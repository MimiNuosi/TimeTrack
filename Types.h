// TimeTrack - 共享数据结构定义
// 所有模块共享的基础类型，无外部依赖
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <windows.h>

// ============================================================================
// WindowInfo — 窗口监测快照
// ============================================================================
struct WindowInfo {
    HWND         hwnd         = nullptr; // 窗口句柄
    DWORD        processId    = 0;       // 所属进程 ID
    std::wstring processPath;            // 完整 exe 路径（如 C:\...\chrome.exe）
    std::wstring windowTitle;            // GetWindowTextW 原始标题
    bool         isMinimized  = false;   // IsIconic(hwnd)
    bool         isForeground = false;   // hwnd == GetForegroundWindow()
};

// ============================================================================
// AppTimeEntry — 单条应用计时记录
// ============================================================================
struct AppTimeEntry {
    std::wstring appPath;                        // exe 完整路径，作为唯一标识
    std::wstring displayName;                    // 解析后的友好名称
    uint64_t     seconds     = 0;                // 累计秒数
    std::chrono::system_clock::time_point lastUpdate; // 最后更新时间
};

// ============================================================================
// DailyData — 单日统计汇总（对应 JSON 中一个日期条目）
// ============================================================================
struct DailyData {
    std::string              date;         // "YYYY-MM-DD"
    uint64_t                 totalSeconds = 0;
    std::vector<AppTimeEntry> entries;

    // 按秒数降序查找条目
    int FindEntryIndex(const std::wstring& appPath) const {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].appPath == appPath) return static_cast<int>(i);
        }
        return -1;
    }
};

// ============================================================================
// IgnoreEntry — 忽略列表条目（含添加时间，满足 PRD F-14）
// ============================================================================
struct IgnoreEntry {
    std::wstring appPath;
    std::chrono::system_clock::time_point addedAt;
};

// ============================================================================
// AppConfig — 应用配置（对应 JSON 中 config 段）
// ============================================================================
struct AppConfig {
    bool autoStart              = true;   // 开机自启（F-16）
    int  pollingInterval        = 2;      // 检测频率 1-10 秒（F-17，默认2）
    int  idleThresholdMinutes   = 5;      // 空闲阈值 1-30 分钟（F-18，默认5）
    int  retentionDays          = 90;     // 数据保留天数
    bool idleDetectionEnabled   = true;   // 是否启用空闲检测
    bool statisticsPaused       = false;  // 全局暂停统计

    // 忽略列表
    std::vector<IgnoreEntry> ignoreList;

    // 辅助：检查某路径是否在忽略列表中
    bool IsPathIgnored(const std::wstring& appPath) const {
        for (const auto& ie : ignoreList) {
            if (_wcsicmp(ie.appPath.c_str(), appPath.c_str()) == 0)
                return true;
        }
        return false;
    }
};

// ============================================================================
// 工具：宽字符串与当前时钟的辅助
// ============================================================================
inline std::chrono::system_clock::time_point SystemNow() {
    return std::chrono::system_clock::now();
}
