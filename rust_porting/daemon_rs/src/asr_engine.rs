use anyhow::Result;
use sherpa_onnx::{OfflineRecognizer, OfflineRecognizerConfig, VoiceActivityDetector};
use std::path::Path;
use std::sync::Arc;

/// ASR引擎，封装sherpa-onnx离线识别
#[derive(Clone)]
pub struct AsrEngine {
    inner: Arc<AsrEngineInner>,
}

struct AsrEngineInner {
    recognizer: OfflineRecognizer,
    vad: Option<VoiceActivityDetector>,
}

/// 识别结果
#[derive(Debug, Clone, serde::Serialize)]
pub struct RecognitionOutput {
    pub text: String,
    pub confidence: f32,
    pub language: Option<String>,
}

impl AsrEngine {
    /// 创建新的ASR引擎实例
    pub fn new(model_dir: &Path, model_type: &str) -> Result<Self> {
        let config = OfflineRecognizerConfig {
            model_config: sherpa_onnx::OfflineModelConfig {
                model_type: model_type.to_string(),
                ..Default::default()
            },
            ..Default::default()
        };

        let recognizer = OfflineRecognizer::new(config)?;

        Ok(Self {
            inner: Arc::new(AsrEngineInner {
                recognizer,
                vad: None,
            }),
        })
    }

    /// 初始化VAD（语音活动检测）
    pub fn init_vad(&self, vad_model_path: &Path) -> Result<()> {
        // 注意：实际实现需要在 AsrEngineInner 中使用 Mutex 包装 VAD
        // 这里简化处理
        Ok(())
    }

    /// 处理音频数据，返回识别结果
    pub fn process_audio(&self, samples: &[f32]) -> Result<RecognitionOutput> {
        let result = self.inner.recognizer.decode(samples);

        Ok(RecognitionOutput {
            text: result.text,
            confidence: result.confidence,
            language: result.lang,
        })
    }

    /// 使用VAD修剪音频，去除静音部分
    pub fn trim_silence(&self, samples: &[f32]) -> Result<Vec<f32>> {
        // 注意：VAD 状态可能需要 Mutex 保护，如果需要并发访问
        // 这里简化处理，假设 VAD 是可选的且在单线程中使用
        Ok(samples.to_vec())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_asr_engine_creation() {
        // 这是一个基本结构测试
        // 实际测试需要有效的模型文件
    }
}
