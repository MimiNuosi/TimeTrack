// TimeTrack - 计时引擎（核心）
// 职责：每秒 Tick 判定是否累计时间，管理应用切换、跨天分割、暂停恢复
// 依赖：WindowMonitor, IdleDetector, DataStore, ConfigManager, IgnoreManager, AppNameResolver

#pragma once
#include "Types.h"
#include <chrono>
#include <string>

class DataStore;
class WindowMonitor;
class IdleDetector;
class ConfigManager;
class IgnoreManager;
class AppNameResolver;

class TimerEngine {
public:
    // 构造函数：注入所有依赖
    TimerEngine(WindowMonitor& wm, IdleDetector& id, DataStore& ds,
                ConfigManager& cm, IgnoreManager& im, AppNameResolver& ar);

    TimerEngine(const TimerEngine&) = delete;
    TimerEngine& operator=(const TimerEngine&) = delete;

    // ---- 主逻辑入口 ----
    // 每次定时器触发时调用（轮询模式：由 main loop 调用）
    // 内部使用 QueryPerformanceCounter 精确测量间隔
    void Tick();

    // ---- 暂停/恢复 ----
    void Pause();
    void Resume();
    bool IsPaused() const { return m_paused; }

    // ---- 紧急持久化 ----
    // 退出/关机时调用，立即保存数据并写盘
    void Flush();

    // ---- 查询（供 UI / 验证使用） ----
    const DailyData& GetTodayData() const { return m_today; }
    const std::wstring& GetCurrentAppPath() const { return m_currentAppPath; }
    const std::wstring& GetCurrentDisplayName() const { return m_currentDisplayName; }

    // ---- 忽略列表变化通知 ----
    // 当应用被添加到忽略列表时，立即停止对该应用的追踪
    void OnAppIgnored(const std::wstring& appPath);

private:
    // 综合判定：当前窗口是否可以累积时间
    bool CanAccumulate(const WindowInfo& win, const std::wstring& appPath) const;

    // 切换到新应用：保存旧应用的累计秒数，重置新应用状态
    void SwitchToApp(const std::wstring& appPath, const std::wstring& displayName);

    // 在 m_today.entries 中查找或创建条目
    AppTimeEntry* FindOrCreateEntry(const std::wstring& appPath, const std::wstring& displayName);

    // 检查并处理跨天分割
    void CheckDayBoundary();

    // 获取当前日期字符串 "YYYY-MM-DD"
    std::string TodayDateString() const;

    // 格式化秒数为 HH:MM:SS（用于调试输出）
    static std::string FormatSeconds(uint64_t totalSec);

    // ---- 依赖引用 ----
    WindowMonitor&   m_windowMonitor;
    IdleDetector&    m_idleDetector;
    DataStore&       m_dataStore;
    ConfigManager&   m_config;
    IgnoreManager&   m_ignoreManager;
    AppNameResolver& m_nameResolver;

    // ---- 计时状态 ----
    bool              m_paused            = false;
    std::wstring      m_currentAppPath;       // 当前正在跟踪的应用路径
    std::wstring      m_currentDisplayName;   // 当前应用友好名称
    DailyData         m_today;                // 今日累计数据（内存中实时更新）
    uint64_t          m_unsavedSeconds    = 0; // 自上次 Save 以来新增秒数
    bool              m_firstTick         = true; // 首次 Tick 标志

    // ---- 高精度计时（QueryPerformanceCounter） ----
    LARGE_INTEGER     m_qpcFrequency;
    LARGE_INTEGER     m_lastTickQPC;
    double            m_carrySeconds      = 0.0; // 未满1秒的小数累积（避免截断误差）

    // 持久化间隔（秒）
    static constexpr uint64_t SAVE_INTERVAL_SECONDS = 300; // 5分钟
};
