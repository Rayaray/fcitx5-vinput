use zbus::{interface, SignalContext};
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;

/// 结果来源类型
pub const SOURCE_RAW: &str = "raw";
pub const SOURCE_LLM: &str = "llm";
pub const SOURCE_ASR: &str = "asr";
pub const SOURCE_CANCEL: &str = "cancel";

/// 候选结果
#[derive(Debug, Clone, Serialize, Deserialize, zvariant::Type)]
pub struct Candidate {
    pub text: String,
    pub source: String,
}

/// 识别结果信号的数据结构 (对齐 C++ 版本的 vinput::result::Payload)
#[derive(Debug, Serialize, Deserialize, zvariant::Type)]
pub struct RecognitionResult {
    /// 提交的文本
    pub commit_text: String,
    /// 候选列表
    pub candidates: Vec<Candidate>,
}

impl RecognitionResult {
    /// 从纯文本创建结果
    pub fn from_text(text: String, source: &str) -> Self {
        let candidate = Candidate {
            text: text.clone(),
            source: source.to_string(),
        };
        Self {
            commit_text: text,
            candidates: vec![candidate],
        }
    }

    /// 创建空结果
    pub fn empty() -> Self {
        Self {
            commit_text: String::new(),
            candidates: Vec::new(),
        }
    }
}

/// 状态枚举，对齐 C++ 版本
#[derive(Debug, Clone, Copy, Serialize, Deserialize, zvariant::Type)]
pub enum DaemonStatus {
    Idle,
    Recording,
    Inferring,
    Postprocessing,
    Error,
}

impl DaemonStatus {
    pub fn as_str(&self) -> &'static str {
        match self {
            DaemonStatus::Idle => "idle",
            DaemonStatus::Recording => "recording",
            DaemonStatus::Inferring => "inferring",
            DaemonStatus::Postprocessing => "postprocessing",
            DaemonStatus::Error => "error",
        }
    }
}

/// 结构化错误信息 (对齐 C++ 版本)
#[derive(Debug, Clone, Serialize, Deserialize, zvariant::Type)]
pub struct DaemonErrorInfo {
    /// 错误代码
    pub code: String,
    /// 错误主体/来源
    pub subject: String,
    /// 详细信息
    pub detail: String,
    /// 时间戳 (ISO 8601 格式)
    pub timestamp: String,
}

impl DaemonErrorInfo {
    pub fn new(code: &str, subject: &str, detail: &str) -> Self {
        let timestamp = chrono::Utc::now().to_rfc3339();
        Self {
            code: code.to_string(),
            subject: subject.to_string(),
            detail: detail.to_string(),
            timestamp,
        }
    }

    pub fn asr_error(detail: &str) -> Self {
        Self::new("asr_error", "ASR", detail)
    }

    pub fn llm_error(detail: &str) -> Self {
        Self::new("llm_error", "LLM", detail)
    }

    pub fn audio_error(detail: &str) -> Self {
        Self::new("audio_error", "Audio", detail)
    }
}

/// 发送给后台任务的命令
#[derive(Debug)]
pub enum DaemonCommand {
    StartRecording,
    StartCommandRecording { context_text: String },
    StopRecording { reason: String },
}

pub struct VinputDBusService {
    command_tx: mpsc::Sender<DaemonCommand>,
}

impl VinputDBusService {
    pub fn new(command_tx: mpsc::Sender<DaemonCommand>) -> Self {
        Self { command_tx }
    }
}

#[interface(name = "org.fcitx.Vinput.Service")]
impl VinputDBusService {
    /// 开始普通录音
    async fn start_recording(&self) -> zbus::fdo::Result<()> {
        tracing::info!("收到 StartRecording 请求");
        self.command_tx.send(DaemonCommand::StartRecording).await
            .map_err(|e| zbus::fdo::Error::Failed(e.to_string()))?;
        Ok(())
    }

    /// 开始带上下文的命令录音
    async fn start_command_recording(&self, context_text: String) -> zbus::fdo::Result<()> {
        tracing::info!("收到 StartCommandRecording 请求, 上下文: {}", context_text);
        self.command_tx.send(DaemonCommand::StartCommandRecording { context_text }).await
            .map_err(|e| zbus::fdo::Error::Failed(e.to_string()))?;
        Ok(())
    }

    /// 停止录音并获取结果
    async fn stop_recording(&self, reason: String) -> zbus::fdo::Result<String> {
        tracing::info!("收到 StopRecording 请求, 原因: {}", reason);
        self.command_tx.send(DaemonCommand::StopRecording { reason }).await
            .map_err(|e| zbus::fdo::Error::Failed(e.to_string()))?;
        // 返回 JSON 格式的结果，对齐 C++ 逻辑
        Ok("{\"status\": \"ok\"}".to_string())
    }

    /// 获取当前状态
    async fn get_status(&self) -> zbus::fdo::Result<String> {
        // 这里需要从状态管理器获取真实状态，原型中先返回 idle
        Ok(DaemonStatus::Idle.as_str().to_string())
    }

    // --- 信号 (Signals) ---

    /// 识别结果信号
    #[zbus(signal)]
    pub async fn recognition_result(ctx: &SignalContext<'_>, result_json: String) -> zbus::Result<()>;

    /// 状态变更信号
    #[zbus(signal)]
    pub async fn status_changed(ctx: &SignalContext<'_>, status: String) -> zbus::Result<()>;

    /// 错误信号 (结构化错误信息)
    /// 签名: (ssss) - code, subject, detail, timestamp
    #[zbus(signal)]
    pub async fn daemon_error(
        ctx: &SignalContext<'_>,
        code: String,
        subject: String,
        detail: String,
        timestamp: String,
    ) -> zbus::Result<()>;
}


impl Default for VinputDBusService {
    fn default() -> Self {
        Self::new().unwrap()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_dbus_service_creation() {
        let service = VinputDBusService::new().unwrap();
        assert_eq!(service.connection_name, "org.fcitx.Vinput");
    }

    #[tokio::test]
    async fn test_handle_status_request() {
        let service = VinputDBusService::new().unwrap();
        let response = service.handle_method(DBusMethod::GetStatus).await.unwrap();

        let status: StatusResponse = serde_json::from_str(&response).unwrap();
        assert!(!status.is_recording);
    }
}
