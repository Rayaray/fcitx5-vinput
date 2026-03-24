use anyhow::Result;
use serde::{Deserialize, Serialize};
use tracing::{info, warn};

use crate::dbus_service::{RecognitionResult, Candidate, SOURCE_ASR, SOURCE_LLM};

/// LLM 提供者配置
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LlmConfig {
    /// API 端点 URL
    pub endpoint: Option<String>,
    /// API 密钥
    pub api_key: Option<String>,
    /// 模型名称
    pub model: Option<String>,
    /// 最大 tokens
    pub max_tokens: Option<u32>,
    /// 温度
    pub temperature: Option<f32>,
}

impl Default for LlmConfig {
    fn default() -> Self {
        Self {
            endpoint: None,
            api_key: None,
            model: None,
            max_tokens: Some(100),
            temperature: Some(0.7),
        }
    }
}

/// 场景配置
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SceneConfig {
    /// 场景 ID
    pub id: String,
    /// 提示词
    pub prompt: Option<String>,
    /// 候选数量
    pub candidate_count: Option<u32>,
    /// LLM 提供者 ID
    pub provider_id: Option<String>,
}

/// LLM 后处理器
/// 
/// 负责 ASR 结果的 LLM 后处理，包括：
/// - 文本纠错
/// - 标点添加
/// - 命令模式处理
#[derive(Clone)]
pub struct PostProcessor {
    config: LlmConfig,
    configured: bool,
}

impl PostProcessor {
    /// 创建新的后处理器
    pub fn new() -> Self {
        Self {
            config: LlmConfig::default(),
            configured: false,
        }
    }

    /// 使用配置创建后处理器
    pub fn with_config(config: LlmConfig) -> Self {
        let configured = config.endpoint.is_some() && config.api_key.is_some();
        Self { config, configured }
    }

    /// 检查是否已配置 LLM
    pub fn is_configured(&self) -> bool {
        self.configured
    }

    /// 更新配置
    pub fn update_config(&mut self, config: LlmConfig) {
        self.configured = config.endpoint.is_some() && config.api_key.is_some();
        self.config = config;
    }

    /// 处理普通文本
    /// 
    /// 对 ASR 结果进行后处理，如添加标点、纠错等
    pub async fn process(&self, text: &str) -> Result<RecognitionResult> {
        if !self.configured {
            // 未配置 LLM，直接返回 ASR 结果
            return Ok(RecognitionResult::from_text(text.to_string(), SOURCE_ASR));
        }

        info!("LLM 后处理: {}", text);

        // TODO: 实际的 LLM API 调用
        // 这里是一个框架实现，实际应该：
        // 1. 构建 prompt (如: "为以下文本添加标点：{text}")
        // 2. 调用 LLM API
        // 3. 解析响应
        
        // 模拟处理结果
        let processed_text = self.simulate_process(text);
        
        Ok(RecognitionResult {
            commit_text: processed_text.clone(),
            candidates: vec![
                Candidate {
                    text: processed_text,
                    source: SOURCE_LLM.to_string(),
                },
            ],
        })
    }

    /// 处理命令模式
    /// 
    /// 结合上下文处理用户的语音命令
    pub async fn process_command(&self, asr_text: &str, selected_text: &str) -> Result<RecognitionResult> {
        if !self.configured {
            // 未配置 LLM，返回原始 ASR 结果
            return Ok(RecognitionResult::from_text(asr_text.to_string(), SOURCE_ASR));
        }

        info!("LLM 命令处理: ASR='{}', 上下文='{}'", asr_text, 
            if selected_text.len() > 50 {
                format!("{}...", &selected_text[..50])
            } else {
                selected_text.to_string()
            }
        );

        // TODO: 实际的 LLM API 调用
        // 命令模式通常需要更复杂的 prompt，如：
        // "用户选择了文本 '{selected_text}'，然后说了 '{asr_text}'。
        //  请理解用户的意图并返回相应的操作结果。"

        let processed_text = self.simulate_command_process(asr_text, selected_text);
        
        Ok(RecognitionResult {
            commit_text: processed_text.clone(),
            candidates: vec![
                Candidate {
                    text: processed_text,
                    source: SOURCE_LLM.to_string(),
                },
            ],
        })
    }

    /// 模拟文本处理 (框架实现)
    fn simulate_process(&self, text: &str) -> String {
        // 简单模拟：添加基本标点
        let trimmed = text.trim();
        if trimmed.is_empty() {
            return String::new();
        }

        // 如果已经有标点，直接返回
        if trimmed.ends_with('。') || trimmed.ends_with('！') || trimmed.ends_with('？') 
            || trimmed.ends_with('.') || trimmed.ends_with('!') || trimmed.ends_with('?') {
            return trimmed.to_string();
        }

        // 添加句号
        format!("{}。", trimmed)
    }

    /// 模拟命令处理 (框架实现)
    fn simulate_command_process(&self, asr_text: &str, _selected_text: &str) -> String {
        // 简单模拟：返回 ASR 文本
        asr_text.to_string()
    }
}

impl Default for PostProcessor {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_post_processor_creation() {
        let processor = PostProcessor::new();
        assert!(!processor.is_configured());
    }

    #[test]
    fn test_post_processor_with_config() {
        let config = LlmConfig {
            endpoint: Some("https://api.example.com/v1/chat".to_string()),
            api_key: Some("test-key".to_string()),
            model: Some("gpt-4".to_string()),
            ..Default::default()
        };
        
        let processor = PostProcessor::with_config(config);
        assert!(processor.is_configured());
    }

    #[tokio::test]
    async fn test_process_without_config() {
        let processor = PostProcessor::new();
        let result = processor.process("你好世界").await.unwrap();
        
        assert_eq!(result.commit_text, "你好世界");
        assert_eq!(result.candidates.len(), 1);
        assert_eq!(result.candidates[0].source, SOURCE_ASR);
    }

    #[tokio::test]
    async fn test_process_with_config() {
        let config = LlmConfig {
            endpoint: Some("https://api.example.com/v1/chat".to_string()),
            api_key: Some("test-key".to_string()),
            ..Default::default()
        };
        
        let processor = PostProcessor::with_config(config);
        let result = processor.process("你好世界").await.unwrap();
        
        // 模拟处理会添加标点
        assert!(result.commit_text.ends_with('。'));
        assert_eq!(result.candidates[0].source, SOURCE_LLM);
    }
}
