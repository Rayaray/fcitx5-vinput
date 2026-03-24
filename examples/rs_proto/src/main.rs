mod asr_engine;
mod audio_capture;
mod dbus_service;
mod post_processor;

use zbus::ConnectionBuilder;
use anyhow::Result;
use asr_engine::AsrEngine;
use audio_capture::{AudioCapture, MIN_SAMPLES_FOR_INFERENCE, audio_utils};
use dbus_service::{VinputDBusService, DaemonCommand, DaemonStatus, RecognitionResult, DaemonErrorInfo, Candidate, SOURCE_ASR, SOURCE_LLM};
use post_processor::PostProcessor;
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
    audio_capture: Option<AudioCapture>,
    asr_engine: Option<AsrEngine>,
}

impl VinputDaemon {
    async fn new() -> Result<Self> {
        let (command_tx, command_rx) = mpsc::channel(32);

        Ok(Self {
            command_tx,
            command_rx,
            audio_capture: None,
            asr_engine: None,
        })
    }

    /// 初始化ASR引擎
    fn init_asr(&mut self, model_dir: PathBuf, model_type: &str) -> Result<()> {
        info!("初始化ASR引擎: {:?}", model_dir);
        let engine = AsrEngine::new(&model_dir, model_type)?;
        self.asr_engine = Some(engine);
        Ok(())
    }

    /// 初始化音频捕获
    fn init_audio(&mut self) -> Result<()> {
        info!("初始化音频捕获");
        let (capture, _receiver) = AudioCapture::new(16000, 1)?;
        self.audio_capture = Some(capture);
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
        
        tokio::spawn(async move {
            let mut current_state = DaemonState::Idle;
            let mut audio_buffer = Vec::new();

            while let Some(cmd) = command_rx.recv().await {
                match cmd {
                    DaemonCommand::StartRecording => {
                        if current_state != DaemonState::Idle {
                            continue;
                        }
                        info!("后台任务: 开始录音...");
                        current_state = DaemonState::Recording;
                        audio_buffer.clear();

                        let _ = VinputDBusService::status_changed(
                            interface_ref.signal_context(),
                            current_state.to_status().as_str().to_string()
                        ).await;
                    }
                    DaemonCommand::StartCommandRecording { context_text } => {
                        info!("后台任务: 开始命令录音, 上下文: {}", context_text);
                        // 类似 StartRecording，但可能需要不同的 ASR 配置
                    }
                    DaemonCommand::StopRecording { reason } => {
                        if current_state != DaemonState::Recording {
                            continue;
                        }
                        info!("后台任务: 停止录音, 原因: {}", reason);
                        current_state = DaemonState::Inferring;

                        let _ = VinputDBusService::status_changed(
                            interface_ref.signal_context(),
                            current_state.to_status().as_str().to_string()
                        ).await;

                        // 执行 ASR 识别
                        if let Some(engine) = asr_engine.clone() {
                            let samples = audio_buffer.clone();
                            let interface_ref = interface_ref.clone();
                            
                            tokio::spawn(async move {
                                match engine.process_audio(&samples) {
                                    Ok(output) => {
                                        let result = RecognitionResult {
                                            text: output.text,
                                            is_final: true,
                                            confidence: output.confidence,
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
                                            format!("ASR 识别失败: {}", e)
                                        ).await;
                                    }
                                }
                            });
                        }

                        current_state = DaemonState::Idle;
                        let _ = VinputDBusService::status_changed(
                            interface_ref.signal_context(),
                            current_state.to_status().as_str().to_string()
                        ).await;
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
    info!("  Vinput Daemon (Rust 原型)");
    info!("  fcitx5-vinput 移植示例");
    info!("================================");

    let daemon = VinputDaemon::new().await?;
    daemon.run().await?;

    info!("Daemon已关闭");
    Ok(())
}
