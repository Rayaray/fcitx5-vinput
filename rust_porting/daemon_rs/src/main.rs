mod asr_engine;
mod audio_capture;
mod dbus_service;
mod post_processor;
mod adaptor_manager;

use zbus::ConnectionBuilder;
use anyhow::Result;
use asr_engine::AsrEngine;
use audio_capture::{AudioCapture, MIN_SAMPLES_FOR_INFERENCE, audio_utils};
use dbus_service::{VinputDBusService, DaemonCommand, DaemonStatus, RecognitionResult, DaemonErrorInfo, Candidate, SOURCE_ASR, SOURCE_LLM};
use post_processor::PostProcessor;
use adaptor_manager::AdaptorManager;
use std::path::PathBuf;
use tokio::signal;
use tokio::sync::mpsc;
use tracing::{info, error, warn};

/// Daemon 状态机 (对齐 C++ 版本)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum DaemonState {
    Idle,
    Recording,
    Inferring,
    Postprocessing,
}

impl DaemonState {
    fn to_status(&self) -> DaemonStatus {
        match self {
            DaemonState::Idle => DaemonStatus::Idle,
            DaemonState::Recording => DaemonStatus::Recording,
            DaemonState::Inferring => DaemonStatus::Inferring,
            DaemonState::Postprocessing => DaemonStatus::Postprocessing,
        }
    }
}

/// Daemon主结构
struct VinputDaemon {
    command_tx: mpsc::Sender<DaemonCommand>,
    command_rx: mpsc::Receiver<DaemonCommand>,
    audio_capture: AudioCapture,
    asr_engine: Option<AsrEngine>,
    post_processor: PostProcessor,
    adaptor_manager: AdaptorManager,
}

impl VinputDaemon {
    async fn new() -> Result<Self> {
        let (command_tx, command_rx) = mpsc::channel(32);
        
        // 初始化音频捕获器
        let audio_capture = AudioCapture::new(16000, 1)?;
        
        Ok(Self {
            command_tx,
            command_rx,
            audio_capture,
            asr_engine: None,
            post_processor: PostProcessor::new(),
            adaptor_manager: AdaptorManager::new(),
        })
    }

    /// 初始化ASR引擎
    fn init_asr(&mut self, model_dir: PathBuf, model_type: &str) -> Result<()> {
        info!("初始化ASR引擎: {:?}", model_dir);
        let engine = AsrEngine::new(&model_dir, model_type)?;
        self.asr_engine = Some(engine);
        Ok(())
    }

    /// 启动Daemon
    async fn run(mut self) -> Result<()> {
        info!("Vinput Daemon 启动中...");

        // 初始化 ASR 引擎 (示例路径)
        let asr_engine = AsrEngine::new(std::path::Path::new("models"), "whisper")
            .map_err(|e| {
                error!("无法初始化 ASR 引擎: {}", e);
                e
            })?;
        self.asr_engine = Some(asr_engine);

        let dbus_service = VinputDBusService::new(self.command_tx.clone());

        // 启动DBus服务并注册对象
        let conn = ConnectionBuilder::session()?
            .name("org.fcitx.Vinput")?
            .serve_at("/org/fcitx/Vinput", dbus_service)?
            .build()
            .await?;

        info!("DBus服务已启动并注册到 org.fcitx.Vinput");

        let object_server = conn.object_server();
        let interface_ref = object_server.interface::<_, VinputDBusService>("/org/fcitx/Vinput").await?;

        // 后台任务：处理 ASR 和音频逻辑
        let mut command_rx = self.command_rx;
        let mut asr_engine = self.asr_engine.take();
        let audio_capture = self.audio_capture;
        let post_processor = self.post_processor;

        tokio::spawn(async move {
            let mut current_state = DaemonState::Idle;
            let mut is_command_mode = false;
            let mut selected_text = String::new();

            while let Some(cmd) = command_rx.recv().await {
                match cmd {
                    DaemonCommand::StartRecording => {
                        if current_state != DaemonState::Idle {
                            warn!("忽略 StartRecording，当前状态: {:?}", current_state);
                            continue;
                        }
                        info!("后台任务: 开始录音...");
                        
                        if let Err(e) = audio_capture.begin_recording() {
                            error!("无法开始录音: {}", e);
                            let _ = VinputDBusService::daemon_error(
                                interface_ref.signal_context(),
                                "audio_error".to_string(),
                                "Audio".to_string(),
                                format!("无法开始录音: {}", e),
                                chrono::Utc::now().to_rfc3339(),
                            ).await;
                            continue;
                        }
                        
                        current_state = DaemonState::Recording;
                        is_command_mode = false;
                        selected_text.clear();
                        
                        let _ = VinputDBusService::status_changed(
                            interface_ref.signal_context(),
                            current_state.to_status().as_str().to_string()
                        ).await;
                    }
                    
                    DaemonCommand::StartCommandRecording { context_text } => {
                        if current_state != DaemonState::Idle {
                            warn!("忽略 StartCommandRecording，当前状态: {:?}", current_state);
                            continue;
                        }
                        info!("后台任务: 开始命令录音, 上下文长度: {} 字符", context_text.len());
                        
                        if let Err(e) = audio_capture.begin_recording() {
                            error!("无法开始命令录音: {}", e);
                            let _ = VinputDBusService::daemon_error(
                                interface_ref.signal_context(),
                                "audio_error".to_string(),
                                "Audio".to_string(),
                                format!("无法开始命令录音: {}", e),
                                chrono::Utc::now().to_rfc3339(),
                            ).await;
                            continue;
                        }
                        
                        current_state = DaemonState::Recording;
                        is_command_mode = true;
                        selected_text = context_text;
                        
                        let _ = VinputDBusService::status_changed(
                            interface_ref.signal_context(),
                            current_state.to_status().as_str().to_string()
                        ).await;
                    }
                    
                    DaemonCommand::StopRecording { reason } => {
                        if current_state != DaemonState::Recording {
                            warn!("忽略 StopRecording，当前状态: {:?}", current_state);
                            continue;
                        }
                        info!("后台任务: 停止录音, 原因: {}", reason);
                        
                        let pcm_samples = audio_capture.stop_and_get_buffer();
                        
                        // 检查最小采样数
                        if pcm_samples.len() < MIN_SAMPLES_FOR_INFERENCE {
                            warn!(
                                "录音太短，跳过推理: {} 样本 ({:.1}ms)",
                                pcm_samples.len(),
                                audio_utils::duration_ms(pcm_samples.len(), 16000)
                            );
                            current_state = DaemonState::Idle;
                            let _ = VinputDBusService::status_changed(
                                interface_ref.signal_context(),
                                current_state.to_status().as_str().to_string()
                            ).await;
                            continue;
                        }
                        
                        current_state = DaemonState::Inferring;
                        let _ = VinputDBusService::status_changed(
                            interface_ref.signal_context(),
                            current_state.to_status().as_str().to_string()
                        ).await;

                        if let Some(engine) = asr_engine.clone() {
                            let samples_f32 = audio_utils::i16_to_f32(&pcm_samples);
                            let interface_ref = interface_ref.clone();
                            let post_processor = post_processor.clone();
                            let selected_text_clone = selected_text.clone();
                            let is_cmd = is_command_mode;

                            tokio::spawn(async move {
                                match engine.process_audio(&samples_f32) {
                                    Ok(output) => {
                                        let result = if output.text.is_empty() {
                                            RecognitionResult::empty()
                                        } else {
                                            let needs_postprocess = post_processor.is_configured();
                                            
                                            if needs_postprocess {
                                                let _ = VinputDBusService::status_changed(
                                                    interface_ref.signal_context(),
                                                    DaemonStatus::Postprocessing.as_str().to_string()
                                                ).await;
                                                
                                                let processed = if is_cmd {
                                                    post_processor.process_command(
                                                        &output.text,
                                                        &selected_text_clone
                                                    ).await
                                                } else {
                                                    post_processor.process(&output.text).await
                                                };
                                                
                                                match processed {
                                                    Ok(llm_result) => llm_result,
                                                    Err(e) => {
                                                        error!("LLM 后处理失败: {}", e);
                                                        let _ = VinputDBusService::daemon_error(
                                                            interface_ref.signal_context(),
                                                            "llm_error".to_string(),
                                                            "LLM".to_string(),
                                                            format!("后处理失败: {}", e),
                                                            chrono::Utc::now().to_rfc3339(),
                                                        ).await;
                                                        RecognitionResult::from_text(output.text, SOURCE_ASR)
                                                    }
                                                }
                                            } else {
                                                RecognitionResult::from_text(output.text, SOURCE_ASR)
                                            }
                                        };

                                        if let Ok(result_json) = serde_json::to_string(&result) {
                                            let _ = VinputDBusService::recognition_result(
                                                interface_ref.signal_context(),
                                                result_json
                                            ).await;
                                        }
                                    }
                                    Err(e) => {
                                        error!("ASR 识别失败: {}", e);
                                        let _ = VinputDBusService::daemon_error(
                                            interface_ref.signal_context(),
                                            "asr_error".to_string(),
                                            "ASR".to_string(),
                                            format!("识别失败: {}", e),
                                            chrono::Utc::now().to_rfc3339(),
                                        ).await;
                                    }
                                }
                                
                                let _ = VinputDBusService::status_changed(
                                    interface_ref.signal_context(),
                                    DaemonStatus::Idle.as_str().to_string()
                                ).await;
                            });
                        }
                        
                        current_state = DaemonState::Idle;
                    }
                    
                    DaemonCommand::StartAdaptor { adaptor_id } => {
                        info!("启动 Adaptor: {}", adaptor_id);
                        
                        if let Err(e) = adaptor_manager.start_adaptor(&adaptor_id) {
                            error!("启动 Adaptor {} 失败: {}", adaptor_id, e);
                            let _ = VinputDBusService::daemon_error(
                                interface_ref.signal_context(),
                                "adaptor_error".to_string(),
                                "Adaptor".to_string(),
                                format!("启动 {} 失败: {}", adaptor_id, e),
                                chrono::Utc::now().to_rfc3339(),
                            ).await;
                        }
                    }
                    
                    DaemonCommand::StopAdaptor { adaptor_id } => {
                        info!("停止 Adaptor: {}", adaptor_id);
                        
                        if let Err(e) = adaptor_manager.stop_adaptor(&adaptor_id) {
                            error!("停止 Adaptor {} 失败: {}", adaptor_id, e);
                            let _ = VinputDBusService::daemon_error(
                                interface_ref.signal_context(),
                                "adaptor_error".to_string(),
                                "Adaptor".to_string(),
                                format!("停止 {} 失败: {}", adaptor_id, e),
                                chrono::Utc::now().to_rfc3339(),
                            ).await;
                        }
                    }
                }
            }
        });

        // 等待终止信号
        signal::ctrl_c().await?;
        info!("收到终止信号，正在关闭...");

        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    // 初始化日志
    tracing_subscriber::fmt::init();
    
    info!("================================");
    info!(" Vinput Daemon (Rust 原型)");
    info!(" fcitx5-vinput 移植示例");
    info!("================================");
    
    let daemon = VinputDaemon::new().await?;
    daemon.run().await?;
    
    info!("Daemon已关闭");
    Ok(())
}
