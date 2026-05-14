// TimeTrack - 计时引擎实现
// 核心计时逻辑：
//   1. 每次 Tick 使用 QPC 精确测量间隔
//   2. 综合判定（前台/非最小化/非锁屏/非空闲/非忽略）后累积
//   3. 同一进程多窗口自动合并
//   4. 跨天自动分割
//   5. 暂停/恢复即时响应

#include "TimerEngine.h"
#include "WindowMonitor.h"
#include "IdleDetector.h"
#include "DataStore.h"
#include "ConfigManager.h"
#include "IgnoreManager.h"
#include "AppNameResolver.h"
#include <windows.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

// ============================================================================
// MakeStableKey — 将路径转为小写，作为跨窗口切换的稳定合并 key
// 同一可执行文件的不同窗口/标签页在 NTFS 上大小写可能不同，
// 统一小写后确保它们被正确合并为同一应用条目。
// ============================================================================
static std::wstring MakeStableKey(const std::wstring& s) {
    std::wstring key = s;
    for (wchar_t& c : key) {
        if (c >= L'A' && c <= L'Z') {
            c += (L'a' - L'A');
        }
    }
    return key;
}

// ============================================================================
// 构造函数 — 注入依赖，初始化 QPC
// ============================================================================

TimerEngine::TimerEngine(
    WindowMonitor& wm, IdleDetector& id, DataStore& ds,
    ConfigManager& cm, IgnoreManager& im, AppNameResolver& ar)
    : m_windowMonitor(wm)
    , m_idleDetector(id)
    , m_dataStore(ds)
    , m_config(cm)
    , m_ignoreManager(im)
    , m_nameResolver(ar)
{
    // 初始化高精度计时器
    QueryPerformanceFrequency(&m_qpcFrequency);
    m_lastTickQPC.QuadPart = 0;

    // 初始化今日数据
    std::string today = TodayDateString();
    m_today = m_dataStore.LoadDailyData(today);
    if (m_today.date.empty()) {
        m_today.date = today;
    }

    // 规范化所有加载条目的 appPath，确保与运行时 MakeStableKey 一致。
    // 合并因旧 key 格式（如 pid:xxx_Title）导致的重复条目。
    {
        std::vector<AppTimeEntry> merged;
        for (auto& entry : m_today.entries) {
            std::wstring normalizedKey = MakeStableKey(entry.appPath);
            entry.appPath = normalizedKey; // 更新为规范化 key

            // 查找 merged 中是否已有同 key 的条目
            auto it = std::find_if(merged.begin(), merged.end(),
                [&](const AppTimeEntry& e) { return e.appPath == normalizedKey; });
            if (it != merged.end()) {
                // 重复条目：合并秒数，保留较新的显示名称
                it->seconds += entry.seconds;
                if (entry.lastUpdate > it->lastUpdate) {
                    it->displayName = entry.displayName;
                    it->lastUpdate = entry.lastUpdate;
                }
            } else {
                merged.push_back(std::move(entry));
            }
        }
        m_today.entries = std::move(merged);
    }
}

// ============================================================================
// Tick — 核心计时逻辑（每次定时器触发时调用）
// ============================================================================

void TimerEngine::Tick() {
    // ---- 测量本 Tick 的真实间隔 ----
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (m_firstTick) {
        // 首次 Tick：仅记录时间基准，不累积
        m_lastTickQPC = now;
        m_firstTick = false;
        return;
    }

    // 计算自上次 Tick 以来的实际秒数（double 精度，避免截断误差）
    double elapsedSec = static_cast<double>(now.QuadPart - m_lastTickQPC.QuadPart)
                      / static_cast<double>(m_qpcFrequency.QuadPart);
    m_lastTickQPC = now;

    // ---- 全局暂停检查 ----
    if (m_paused || m_config.GetConfig().statisticsPaused) {
        return;
    }

    // ---- 获取当前窗口信息 ----
    const WindowInfo& win = m_windowMonitor.GetCurrentWindow();

    // 无前台窗口 → 不累计
    if (!win.hwnd) {
        m_currentAppPath.clear();
        m_currentDisplayName.clear();
        return;
    }

    // 提取应用路径（小写 processPath 作为稳定 key，确保同一应用所有窗口合并）
    std::wstring appPath = MakeStableKey(win.processPath);
    if (appPath.empty()) {
        // 无法获取路径 → 使用窗口标题降级，加 "title:" 前缀与路径 key 区分
        appPath = L"title:" + MakeStableKey(win.windowTitle);
    }

    // ---- 判定是否可以累积 ----
    if (!CanAccumulate(win, appPath)) {
        // 条件不满足 → 重置当前跟踪，不累积但 QPC 已更新
        m_currentAppPath.clear();
        m_currentDisplayName.clear();
        return;
    }

    // ---- 解析友好名称 ----
    std::wstring displayName = m_nameResolver.Resolve(win.processPath, win.windowTitle);

    // ---- 应用切换检测 ----
    if (appPath != m_currentAppPath) {
        SwitchToApp(appPath, displayName);
    }

    // ---- 累积时间：实际经过秒数 + 先前累积的小数 ----
    double totalToAdd = elapsedSec + m_carrySeconds;
    uint64_t wholeSeconds = static_cast<uint64_t>(totalToAdd);
    m_carrySeconds = totalToAdd - static_cast<double>(wholeSeconds);

    if (wholeSeconds > 0) {
        // 查找或创建当前应用的条目
        AppTimeEntry* entry = FindOrCreateEntry(m_currentAppPath, m_currentDisplayName);
        if (entry) {
            entry->seconds += wholeSeconds;
            entry->lastUpdate = SystemNow();
            m_today.totalSeconds += wholeSeconds;
            m_unsavedSeconds += wholeSeconds;
        }
    }

    // ---- 跨天检查 ----
    CheckDayBoundary();

    // ---- 定期持久化（每 5 分钟） ----
    if (m_unsavedSeconds >= SAVE_INTERVAL_SECONDS) {
        Flush();
    }

    // ---- 异常保护：单次 Tick 过长 ----
    // 如果 elapsedSec 超过 10 秒（如从休眠恢复），限制累积以防止异常数据
    // 但由于 CanAccumulate 中的 IsSystemBlocked 已拦截休眠场景，此处仅作防御
    if (elapsedSec > 60.0) {
        // 异常长间隔（可能调试断点等）→ 丢弃超额部分
        m_carrySeconds = 0.0;
    }
}

// ============================================================================
// CanAccumulate — 综合判定是否可以累积时间
// ============================================================================

bool TimerEngine::CanAccumulate(const WindowInfo& win, const std::wstring& appPath) const {
    // 1) 窗口必须是非最小化的前台窗口
    if (!win.isForeground || win.isMinimized) {
        return false;
    }

    // 2) 系统阻塞？（锁屏、屏保）
    if (m_idleDetector.IsSystemBlocked()) {
        return false;
    }

    // 3) 显示器关闭？
    if (m_idleDetector.IsDisplayOff()) {
        return false;
    }

    // 4) 用户空闲？（仅在启用空闲检测时）
    if (m_config.GetConfig().idleDetectionEnabled) {
        DWORD idleSec = m_idleDetector.GetIdleSeconds();
        DWORD threshold = static_cast<DWORD>(m_config.GetConfig().idleThresholdMinutes) * 60;
        if (idleSec >= threshold) {
            return false;
        }
    }

    // 5) 应用在忽略列表中？
    if (m_ignoreManager.IsIgnored(appPath)) {
        return false;
    }

    return true;
}

// ============================================================================
// SwitchToApp — 应用切换
// ============================================================================

void TimerEngine::SwitchToApp(const std::wstring& appPath, const std::wstring& displayName) {
    // 清空累积的小数部分（应用切换不继承小数）
    m_carrySeconds = 0.0;

    // 更新当前应用
    m_currentAppPath = appPath;
    m_currentDisplayName = displayName;

    // 确保条目存在于 entries 中
    FindOrCreateEntry(appPath, displayName);
}

// ============================================================================
// FindOrCreateEntry — 查找或创建应用条目
// ============================================================================

AppTimeEntry* TimerEngine::FindOrCreateEntry(
    const std::wstring& appPath, const std::wstring& displayName)
{
    int idx = m_today.FindEntryIndex(appPath);
    if (idx >= 0) {
        return &m_today.entries[idx];
    }

    // 创建新条目
    AppTimeEntry entry;
    entry.appPath = appPath;
    entry.displayName = displayName;
    entry.seconds = 0;
    entry.lastUpdate = SystemNow();
    m_today.entries.push_back(std::move(entry));
    return &m_today.entries.back();
}

// ============================================================================
// CheckDayBoundary — 跨天分割
// ============================================================================

void TimerEngine::CheckDayBoundary() {
    std::string today = TodayDateString();
    if (today == m_today.date) {
        return; // 同一天
    }

    // 跨天了：保存旧日数据，开始新的一天
    m_dataStore.SaveDailyData(m_today);
    m_dataStore.Save(); // 立即持久化旧日数据

    // 重置为新的 DailyData
    m_today = DailyData{};
    m_today.date = today;
    m_unsavedSeconds = 0;
    m_carrySeconds = 0.0;
}

// ============================================================================
// Flush — 紧急持久化
// ============================================================================

void TimerEngine::Flush() {
    if (!m_dataStore.IsLoaded()) return;

    // 先将当前计时写入内存
    m_dataStore.SaveDailyData(m_today);
    m_dataStore.Save();
    m_unsavedSeconds = 0;

    // 定期清理过期历史数据（每 5 分钟触发一次）
    int retentionDays = m_config.GetConfig().retentionDays;
    if (retentionDays > 0) {
        m_dataStore.CleanOldData(retentionDays);
    }
}

// ============================================================================
// Pause / Resume
// ============================================================================

void TimerEngine::Pause() {
    m_paused = true;
    // 暂停时立即保存当前状态
    Flush();
}

void TimerEngine::Resume() {
    m_paused = false;
    // 恢复时重置计时基准，避免累积暂停期间的时间
    m_firstTick = true;
    QueryPerformanceCounter(&m_lastTickQPC);
    m_carrySeconds = 0.0;
}

// ============================================================================
// OnAppIgnored — 忽略列表变化通知
// ============================================================================

void TimerEngine::OnAppIgnored(const std::wstring& appPath) {
    // 如果当前正在跟踪的应用被忽略，立即停止
    if (_wcsicmp(m_currentAppPath.c_str(), appPath.c_str()) == 0) {
        m_currentAppPath.clear();
        m_currentDisplayName.clear();
        m_carrySeconds = 0.0;
    }
}

// ============================================================================
// TodayDateString — 获取今日日期 YYYY-MM-DD
// ============================================================================

std::string TimerEngine::TodayDateString() const {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm lt;
    localtime_s(&lt, &t);
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
        lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    return std::string(buf);
}

// 补零辅助（窄字符版）
static std::string Pad2(uint64_t v) {
    return (v < 10) ? ("0" + std::to_string(v)) : std::to_string(v);
}

// ============================================================================
// FormatSeconds — 秒数 → "HH:MM:SS"（纯 string，零栈缓冲区）
// ============================================================================

std::string TimerEngine::FormatSeconds(uint64_t totalSec) {
    uint64_t h = totalSec / 3600;
    uint64_t m = (totalSec % 3600) / 60;
    uint64_t s = totalSec % 60;
    return Pad2(h) + ":" + Pad2(m) + ":" + Pad2(s);
}
