use anyhow::Result;
use std::sync::Arc;
use tracing::{info, warn};
use parking_lot::Mutex;

/// 最小采样数，用于推理 (对齐 C++ 版本)
/// 16000 Hz * 0.1 秒 = 1600 样本
pub const MIN_SAMPLES_FOR_INFERENCE: usize = 1600;

/// 默认采样率
pub const DEFAULT_SAMPLE_RATE: u32 = 16000;
/// 默认声道数
pub const DEFAULT_CHANNELS: u32 = 1;

/// 音频数据块
#[derive(Debug, Clone)]
pub struct AudioChunk {
    pub samples: Vec<i16>,
    pub sample_rate: u32,
    pub timestamp_ms: u64,
}

/// 音频捕获器配置
#[derive(Debug, Clone)]
pub struct AudioConfig {
    pub sample_rate: u32,
    pub channels: u32,
    pub target_device: Option<String>,
}

impl Default for AudioConfig {
    fn default() -> Self {
        Self {
            sample_rate: DEFAULT_SAMPLE_RATE,
            channels: DEFAULT_CHANNELS,
            target_device: None,
        }
    }
}

/// 音频捕获器状态
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum CaptureState {
    Idle,
    Recording,
}

/// 音频捕获器
/// 
/// 支持 PipeWire (Linux) 和模拟模式
pub struct AudioCapture {
    config: AudioConfig,
    state: Arc<Mutex<CaptureState>>,
    buffer: Arc<Mutex<Vec<i16>>>,
}

impl AudioCapture {
    /// 创建新的音频捕获器
    pub fn new(sample_rate: u32, channels: u32) -> Result<Self> {
        Self::with_config(AudioConfig {
            sample_rate,
            channels,
            target_device: None,
        })
    }

    /// 使用配置创建音频捕获器
    pub fn with_config(config: AudioConfig) -> Result<Self> {
        Ok(Self {
            config,
            state: Arc::new(Mutex::new(CaptureState::Idle)),
            buffer: Arc::new(Mutex::new(Vec::new())),
        })
    }

    /// 设置目标设备
    pub fn set_target_device(&mut self, device: Option<String>) {
        self.config.target_device = device;
    }

    /// 开始录音
    pub fn begin_recording(&self) -> Result<()> {
        let mut state = self.state.lock();
        if *state != CaptureState::Idle {
            warn!("尝试开始录音，但当前状态不是 Idle");
            return Err(anyhow::anyhow!("Already recording"));
        }

        // 清空缓冲区
        self.buffer.lock().clear();
        
        *state = CaptureState::Recording;
        info!("录音开始: 采样率={}Hz, 声道={}", self.config.sample_rate, self.config.channels);

        // TODO: 实际的 PipeWire 实现
        // 在 Linux 上，这里应该使用 pipewire crate 初始化音频流
        // 参见 C++ 版本的 audio_capture.cpp
        
        Ok(())
    }

    /// 结束录音 (停止捕获，但保留缓冲区)
    pub fn end_recording(&self) {
        let mut state = self.state.lock();
        if *state != CaptureState::Recording {
            return;
        }
        
        *state = CaptureState::Idle;
        info!("录音结束");
    }

    /// 停止录音并获取缓冲区数据
    pub fn stop_and_get_buffer(&self) -> Vec<i16> {
        self.end_recording();
        
        let mut buffer = self.buffer.lock();
        let samples = std::mem::take(&mut *buffer);
        
        info!("获取音频缓冲区: {} 样本 ({:.1}ms)", 
            samples.len(), 
            samples.len() as f64 * 1000.0 / self.config.sample_rate as f64
        );
        
        samples
    }

    /// 检查是否正在录音
    pub fn is_recording(&self) -> bool {
        *self.state.lock() == CaptureState::Recording
    }

    /// 获取当前缓冲区大小
    pub fn buffer_size(&self) -> usize {
        self.buffer.lock().len()
    }

    /// 向缓冲区添加音频数据 (内部方法，供回调使用)
    pub fn push_samples(&self, samples: &[i16]) {
        if self.is_recording() {
            self.buffer.lock().extend_from_slice(samples);
        }
    }

    /// 检查缓冲区是否满足最小推理要求
    pub fn has_min_samples(&self) -> bool {
        self.buffer.lock().len() >= MIN_SAMPLES_FOR_INFERENCE
    }

    /// 获取配置
    pub fn config(&self) -> &AudioConfig {
        &self.config
    }
}

/// 音频处理工具
pub mod audio_utils {
    use super::*;

    /// 将 i16 音频数据转换为 f32
    pub fn i16_to_f32(samples: &[i16]) -> Vec<f32> {
        samples.iter().map(|&s| s as f32 / i16::MAX as f32).collect()
    }

    /// 将 f32 音频数据转换为 i16
    pub fn f32_to_i16(samples: &[f32]) -> Vec<i16> {
        samples.iter().map(|&s| (s * i16::MAX as f32) as i16).collect()
    }

    /// 重采样音频到 16kHz（如需要）
    pub fn resample_to_16k(samples: &[f32], original_rate: u32) -> Vec<f32> {
        if original_rate == 16000 {
            return samples.to_vec();
        }

        // 简化实现：线性插值
        let ratio = 16000.0 / original_rate as f32;
        let new_len = (samples.len() as f32 * ratio) as usize;
        let mut resampled = Vec::with_capacity(new_len);

        for i in 0..new_len {
            let src_idx = i as f32 / ratio;
            let src_idx_floor = src_idx.floor() as usize;
            let src_idx_ceil = (src_idx_floor + 1).min(samples.len() - 1);
            let t = src_idx - src_idx_floor as f32;
            let sample = samples[src_idx_floor] * (1.0 - t) + samples[src_idx_ceil] * t;
            resampled.push(sample);
        }

        resampled
    }

    /// 峰值归一化
    pub fn peak_normalize(samples: &mut [f32]) {
        if samples.is_empty() {
            return;
        }

        let max = samples.iter().fold(0.0f32, |acc, &x| acc.max(x.abs()));
        if max > 0.0 {
            for sample in samples.iter_mut() {
                *sample /= max;
            }
        }
    }

    /// 计算音频时长 (毫秒)
    pub fn duration_ms(sample_count: usize, sample_rate: u32) -> f64 {
        sample_count as f64 * 1000.0 / sample_rate as f64
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_audio_conversion() {
        let input = vec![100i16, -100, 0, i16::MAX, i16::MIN];
        let output = audio_utils::i16_to_f32(&input);
        assert!(output[0] > 0.0 && output[0] < 1.0);
        assert!(output[3] > 0.99); // MAX -> ~1.0
    }

    #[test]
    fn test_min_samples_check() {
        let capture = AudioCapture::new(16000, 1).unwrap();
        assert!(!capture.has_min_samples());
        
        // 模拟添加足够样本
        capture.push_samples(&vec![0i16; MIN_SAMPLES_FOR_INFERENCE]);
        assert!(capture.has_min_samples());
    }

    #[test]
    fn test_duration_calculation() {
        // 16000 样本 @ 16kHz = 1 秒 = 1000ms
        assert_eq!(audio_utils::duration_ms(16000, 16000), 1000.0);
        // 1600 样本 @ 16kHz = 0.1 秒 = 100ms
        assert_eq!(audio_utils::duration_ms(1600, 16000), 100.0);
    }
}
