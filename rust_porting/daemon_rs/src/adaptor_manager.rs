use anyhow::Result;
use std::collections::HashMap;
use std::sync::Arc;
use parking_lot::Mutex;
use tracing::{info, warn, error};

use crate::dbus_service::AdaptorStatus;

/// Adaptor 配置
#[derive(Debug, Clone)]
pub struct AdaptorConfig {
    pub id: String,
    pub command: String,
    pub args: Vec<String>,
    pub env: HashMap<String, String>,
}

/// 运行中的 Adaptor 信息
#[derive(Debug)]
struct RunningAdaptor {
    config: AdaptorConfig,
    status: AdaptorStatus,
    pid: Option<u32>,
}

/// Adaptor 管理器
/// 
/// 负责管理外部 LLM adaptor 进程的生命周期：
/// - 启动/停止 adaptor
/// - 监控状态
/// - 处理崩溃重启
pub struct AdaptorManager {
    adaptors: Arc<Mutex<HashMap<String, RunningAdaptor>>>,
}

impl AdaptorManager {
    /// 创建新的 Adaptor 管理器
    pub fn new() -> Self {
        Self {
            adaptors: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// 启动 Adaptor
    pub fn start_adaptor(&self, adaptor_id: &str) -> Result<()> {
        let mut adaptors = self.adaptors.lock();
        
        // 检查是否已存在
        if let Some(adaptor) = adaptors.get(adaptor_id) {
            match adaptor.status {
                AdaptorStatus::Running | AdaptorStatus::Starting => {
                    warn!("Adaptor {} 已在运行中", adaptor_id);
                    return Ok(());
                }
                _ => {}
            }
        }

        info!("启动 Adaptor: {}", adaptor_id);

        // TODO: 实际的进程启动逻辑
        // 1. 从配置加载 adaptor 命令
        // 2. 使用 std::process::Command 启动进程
        // 3. 监控进程状态

        // 模拟启动
        adaptors.insert(adaptor_id.to_string(), RunningAdaptor {
            config: AdaptorConfig {
                id: adaptor_id.to_string(),
                command: String::new(),
                args: Vec::new(),
                env: HashMap::new(),
            },
            status: AdaptorStatus::Running,
            pid: Some(12345), // 模拟 PID
        });

        info!("Adaptor {} 已启动", adaptor_id);
        Ok(())
    }

    /// 停止 Adaptor
    pub fn stop_adaptor(&self, adaptor_id: &str) -> Result<()> {
        let mut adaptors = self.adaptors.lock();
        
        if let Some(adaptor) = adaptors.get_mut(adaptor_id) {
            info!("停止 Adaptor: {}", adaptor_id);
            
            // TODO: 实际的进程停止逻辑
            // 1. 发送终止信号
            // 2. 等待进程退出
            // 3. 超时后强制终止

            adaptor.status = AdaptorStatus::Stopped;
            adaptor.pid = None;
            
            info!("Adaptor {} 已停止", adaptor_id);
        } else {
            warn!("尝试停止不存在的 Adaptor: {}", adaptor_id);
        }

        Ok(())
    }

    /// 获取 Adaptor 状态
    pub fn get_status(&self, adaptor_id: &str) -> AdaptorStatus {
        self.adaptors.lock()
            .get(adaptor_id)
            .map(|a| a.status)
            .unwrap_or(AdaptorStatus::Stopped)
    }

    /// 获取所有运行中的 Adaptor
    pub fn get_running_adaptors(&self) -> Vec<String> {
        self.adaptors.lock()
            .iter()
            .filter(|(_, a)| a.status == AdaptorStatus::Running)
            .map(|(id, _)| id.clone())
            .collect()
    }

    /// 停止所有 Adaptor
    pub fn stop_all(&self) {
        let ids: Vec<String> = self.adaptors.lock().keys().cloned().collect();
        
        for id in ids {
            if let Err(e) = self.stop_adaptor(&id) {
                error!("停止 Adaptor {} 失败: {}", id, e);
            }
        }
    }
}

impl Default for AdaptorManager {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_adaptor_manager() {
        let manager = AdaptorManager::new();
        
        // 启动 adaptor
        manager.start_adaptor("test-adaptor").unwrap();
        assert_eq!(manager.get_status("test-adaptor"), AdaptorStatus::Running);
        
        // 停止 adaptor
        manager.stop_adaptor("test-adaptor").unwrap();
        assert_eq!(manager.get_status("test-adaptor"), AdaptorStatus::Stopped);
    }

    #[test]
    fn test_get_running_adaptors() {
        let manager = AdaptorManager::new();
        
        manager.start_adaptor("adaptor-1").unwrap();
        manager.start_adaptor("adaptor-2").unwrap();
        
        let running = manager.get_running_adaptors();
        assert_eq!(running.len(), 2);
        
        manager.stop_all();
        assert!(manager.get_running_adaptors().is_empty());
    }
}
