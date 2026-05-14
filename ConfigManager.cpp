// TimeTrack - 配置管理实现
// 通过 DataStore 读写单一 JSON 文件中的 config 段

#include "ConfigManager.h"
#include "DataStore.h"
#include <algorithm>  // std::clamp

// ============================================================================
// Load — 从 DataStore 加载配置
// ============================================================================

bool ConfigManager::Load(DataStore& store) {
    m_config = store.LoadConfig();
    return true;
}

// ============================================================================
// Save — 通过 DataStore 持久化
// ============================================================================

bool ConfigManager::Save(DataStore& store) {
    return store.SaveConfig(m_config);
}

// ============================================================================
// 设置项（带钳位验证）
// ============================================================================

void ConfigManager::SetPollingInterval(int seconds) {
    // C++17: std::clamp 钳位到 [1, 10]
    m_config.pollingInterval = std::clamp(seconds, 1, 10);
}

void ConfigManager::SetIdleThreshold(int minutes) {
    // 钳位到 [1, 30]
    m_config.idleThresholdMinutes = std::clamp(minutes, 1, 30);
}

void ConfigManager::SetRetentionDays(int days) {
    // 钳位到 [0, 365]
    // 0 表示 "Forever"（永不自动清除历史数据）
    m_config.retentionDays = std::clamp(days, 0, 365);
}

// ============================================================================
// 忽略列表操作
// ============================================================================

void ConfigManager::AddIgnoredApp(const std::wstring& appPath) {
    if (!m_config.IsPathIgnored(appPath)) {
        m_config.ignoreList.push_back({ appPath, SystemNow() });
    }
}

void ConfigManager::RemoveIgnoredApp(const std::wstring& appPath) {
    auto& list = m_config.ignoreList;
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (_wcsicmp(it->appPath.c_str(), appPath.c_str()) == 0) {
            list.erase(it);
            return;
        }
    }
}
