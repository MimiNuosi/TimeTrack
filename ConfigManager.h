// TimeTrack - 配置管理模块
// 职责：加载、保存、访问应用配置
// 持久化依赖 DataStore（单一 JSON 文件中的 config 段）

#pragma once
#include "Types.h"
#include <string>

class DataStore; // 前向声明，避免循环依赖

class ConfigManager {
public:
    ConfigManager() = default;

    // 从 DataStore 加载配置
    bool Load(DataStore& store);

    // 通过 DataStore 持久化当前配置
    bool Save(DataStore& store);

    // 获取配置只读引用
    const AppConfig& GetConfig() const { return m_config; }

    // ---- 设置项修改 ----
    void SetAutoStart(bool value)              { m_config.autoStart = value; }
    void SetPollingInterval(int seconds);      // 钳位 1-10
    void SetIdleThreshold(int minutes);        // 钳位 1-30
    void SetRetentionDays(int days);           // 钳位 1-365
    void SetIdleDetectionEnabled(bool value)   { m_config.idleDetectionEnabled = value; }
    void SetStatisticsPaused(bool value)       { m_config.statisticsPaused = value; }

    // ---- 忽略列表操作 ----
    void AddIgnoredApp(const std::wstring& appPath);
    void RemoveIgnoredApp(const std::wstring& appPath);
    bool IsIgnored(const std::wstring& appPath) const { return m_config.IsPathIgnored(appPath); }

private:
    AppConfig m_config;
};
