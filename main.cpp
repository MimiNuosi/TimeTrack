// TimeTrack - Phase 5 (Final) 主入口
// 架构：可显示统计面板的主窗口 + 消息泵 + 系统托盘 + 控制台日志
// 全模块集成：WindowMonitor / TimerEngine / IdleDetector / DataStore /
//   ConfigManager / IgnoreManager / AppNameResolver / TrayManager / PanelUI
//
// Phase 5 新增：
//   - 所有设置项即时生效（无需重启）：polling/timer/idle/autoStart/retention
//   - 数据清理：启动时、保存时、定期（每5分钟）自动清除过期数据
//   - 错误处理加固：JSON 损坏日志、目录创建回退、注册表失败忽略
//   - 增强的退出清理日志（逐模块输出）

#include "WindowMonitor.h"
#include "TimerEngine.h"
#include "IdleDetector.h"
#include "DataStore.h"
#include "ConfigManager.h"
#include "IgnoreManager.h"
#include "AppNameResolver.h"
#include "TrayManager.h"
#include "SingleInstance.h"
#include "PanelUI.h"
#include "AppIconCache.h"
#include "IgnoreListDialog.h"
#include "SettingsDialog.h"
#include "Resource.h"
#include "Types.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <windows.h>
#include <wtsapi32.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <objbase.h>
#include <shellapi.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// ============================================================================
// 条件输出代理 — 仅在 --console 模式下输出到控制台，否则静默
// ============================================================================

static bool g_consoleEnabled = false;

struct CoutProxy {
    template<typename T>
    CoutProxy& operator<<(const T& val) {
        if (g_consoleEnabled) std::cout << val;
        return *this;
    }
    CoutProxy& operator<<(std::ostream& (*pf)(std::ostream&)) {
        if (g_consoleEnabled) pf(std::cout);
        return *this;
    }
};
static CoutProxy t_cout;

struct CerrProxy {
    template<typename T>
    CerrProxy& operator<<(const T& val) {
        if (g_consoleEnabled) std::cerr << val;
        return *this;
    }
    CerrProxy& operator<<(std::ostream& (*pf)(std::ostream&)) {
        if (g_consoleEnabled) pf(std::cerr);
        return *this;
    }
};
static CerrProxy t_cerr;

// ============================================================================
// 全局指针 + 实例句柄
// ============================================================================

static const wchar_t* MAIN_WINDOW_CLASS = L"TimeTrack_MainWindow";

static HINSTANCE        g_hInst         = nullptr;
static TimerEngine*     g_engine        = nullptr;
static DataStore*       g_dataStore     = nullptr;
static ConfigManager*   g_configManager = nullptr;
static IgnoreManager*   g_ignoreManager = nullptr;
static WindowMonitor*   g_windowMonitor = nullptr;
static IdleDetector*    g_idleDetector  = nullptr;
static TrayManager*     g_trayManager   = nullptr;
static AppNameResolver* g_nameResolver  = nullptr;
static LONG             g_exStyle      = 0;     // 窗口扩展样式（创建时锁定，显示前复用）

// ============================================================================
// 辅助函数声明
// ============================================================================

bool SetAutoStartReg(bool enable);
bool IsAutoStartEnabled();
std::wstring GetAppDataPath();
std::wstring GetSelfPath();
std::string FormatDuration(uint64_t totalSec);
std::string ToNarrow(const std::wstring& ws);
void UpdateTrayTooltip();
void NavigateToDate(HWND hwnd, const std::string& date);
void IncrementDate(std::string& date, int deltaDays);
static void HandleTimer(WPARAM wParam);  // SEH-protected timer handler
void DeleteTaskbarButton(HWND hwnd);      // ITaskbarList::DeleteTab helper

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
// 开机自启 — 注册表操作
// ============================================================================

bool SetAutoStartReg(bool enable) {
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey);
    if (result != ERROR_SUCCESS) {
        t_cout << "[REG] Failed to open Run key (error " << result << ")\n";
        return false;
    }
    if (enable) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring cmdLine = L"\"" + std::wstring(exePath) + L"\" --background";
        result = RegSetValueExW(hKey, L"TimeTrack", 0, REG_SZ,
            (const BYTE*)cmdLine.c_str(),
            (DWORD)((cmdLine.length() + 1) * sizeof(wchar_t)));
        t_cout << "[REG] Auto-start ENABLED: " << ToNarrow(exePath) << "\n";
    } else {
        result = RegDeleteValueW(hKey, L"TimeTrack");
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
        t_cout << "[REG] Auto-start DISABLED\n";
    }
    RegCloseKey(hKey);
    return (result == ERROR_SUCCESS);
}

bool IsAutoStartEnabled() {
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) return false;
    DWORD type;
    result = RegQueryValueExW(hKey, L"TimeTrack", nullptr, &type, nullptr, nullptr);
    RegCloseKey(hKey);
    return (result == ERROR_SUCCESS);
}

// ============================================================================
// 路径工具
// ============================================================================

std::wstring GetAppDataPath() {
    wchar_t appData[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) == 0)
        return L"C:\\TimeTrackData";
    return std::wstring(appData) + L"\\TimeTrack";
}

std::wstring GetSelfPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

// ============================================================================
// 文本转换
// ============================================================================

std::string FormatDuration(uint64_t totalSec) {
    uint64_t h = totalSec / 3600;
    uint64_t m = (totalSec % 3600) / 60;
    uint64_t s = totalSec % 60;
    std::string result;
    if (h > 0) result += std::to_string(h) + "h ";
    if (m > 0 || h > 0) result += std::to_string(m) + "m ";
    result += std::to_string(s) + "s";
    return result;
}

std::string ToNarrow(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], len, nullptr, nullptr);
    return s;
}

static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return {};
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], len);
    return ws;
}

// ============================================================================
// 日期导航
// ============================================================================

void IncrementDate(std::string& date, int deltaDays) {
    // Parse "YYYY-MM-DD"
    int year, month, day;
    sscanf_s(date.c_str(), "%d-%d-%d", &year, &month, &day);

    // Use system time for conversion
    SYSTEMTIME st = {};
    st.wYear = year;
    st.wMonth = month;
    st.wDay = day;

    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);

    // Add delta (in 100-nanosecond intervals)
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    uli.QuadPart += (LONGLONG)deltaDays * 24LL * 3600LL * 10000000LL;

    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    FileTimeToSystemTime(&ft, &st);

    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    date = buf;
}

void NavigateToDate(HWND hwnd, const std::string& date) {
    if (!g_dataStore) return;

    std::string today = PanelUI::g_currentViewDate;
    // Compare against today
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm lt;
    localtime_s(&lt, &t);
    char todayBuf[16];
    snprintf(todayBuf, sizeof(todayBuf), "%04d-%02d-%02d",
        lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    std::string todayStr(todayBuf);

    if (date == todayStr) {
        // Back to today: resume live mode
        PanelUI::SetHistoryMode(false);
        if (g_engine) {
            PanelUI::RefreshFull(g_engine->GetTodayData());
        }
        SetTimer(hwnd, TIMER_REFRESH_UI, 1000, nullptr);
        t_cout << "[NAV] Back to today, live refresh resumed\n";
    } else {
        // History mode: load from DataStore
        PanelUI::SetHistoryMode(true);
        KillTimer(hwnd, TIMER_REFRESH_UI);
        DailyData histData = g_dataStore->LoadDailyData(date);
        PanelUI::LoadHistoryData(date, histData);
        t_cout << "[NAV] Viewing history: " << date << " (entries: " << histData.entries.size() << ")\n";
    }
}

// ============================================================================
// UpdateTrayTooltip
// ============================================================================

void UpdateTrayTooltip() {
    if (!g_trayManager || !g_engine) return;
    const auto& cfg = g_configManager->GetConfig();
    const auto& today = g_engine->GetTodayData();
    const auto& currentName = g_engine->GetCurrentDisplayName();

    std::wstring tip;
    if (cfg.statisticsPaused || g_engine->IsPaused()) {
        tip = L"TimeTrack [PAUSED]\nToday: " +
              std::wstring(FormatDuration(today.totalSeconds).begin(),
                           FormatDuration(today.totalSeconds).end());
    } else if (currentName.empty()) {
        tip = L"TimeTrack\nToday: " +
              std::wstring(FormatDuration(today.totalSeconds).begin(),
                           FormatDuration(today.totalSeconds).end());
    } else {
        tip = L"TimeTrack - " + currentName + L"\nToday: " +
              std::wstring(FormatDuration(today.totalSeconds).begin(),
                           FormatDuration(today.totalSeconds).end());
    }
    g_trayManager->UpdateTooltip(tip);
}

// ============================================================================
// HandleTimer — SEH 保护的定时器处理（独立函数以避免 C2712）
// 注：必须在独立函数中使用 __try/__except，不能与含析构函数的 C++ 对象同框
// ============================================================================

static void HandleTimer(WPARAM wParam) {
    __try {
        if (wParam == TIMER_POLLING) {
            if (g_windowMonitor) g_windowMonitor->Refresh();
            if (g_engine) g_engine->Tick();
            UpdateTrayTooltip();
        }
        else if (wParam == TIMER_REFRESH_UI) {
            if (!PanelUI::g_isHistoryMode && g_engine) {
                PanelUI::RefreshIncremental(
                    g_engine->GetTodayData(),
                    g_engine->GetCurrentAppPath(),
                    g_engine->GetCurrentDisplayName());
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringW(L"[FATAL] WM_TIMER crashed (STATUS_STACK_BUFFER_OVERRUN) — exception swallowed\n");
    }
}

// ============================================================================
// WndProc — 主窗口消息处理
// ============================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

    switch (msg) {

    // ---- 窗口创建 ----
    case WM_CREATE:
    {
        t_cout << "[WM_CREATE] Initializing modules...\n";

        // 0. 创建字体（Segoe UI 9pt）
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");

        // 1. 数据存储
        g_dataStore = new DataStore();
        g_dataStore->SetDataDirectory(GetAppDataPath());
        g_dataStore->Load();
        t_cout << "[INIT] Data directory: " << ToNarrow(GetAppDataPath()) << "\n";

        // 2. 配置管理
        g_configManager = new ConfigManager();
        g_configManager->Load(*g_dataStore);
        const auto& cfg = g_configManager->GetConfig();
        t_cout << "[INIT] Polling interval: " << cfg.pollingInterval << "s\n";
        t_cout << "[INIT] Idle threshold: " << cfg.idleThresholdMinutes << " min\n";
        t_cout << "[INIT] Data retention: " << cfg.retentionDays
                  << (cfg.retentionDays > 0 ? " days" : " (Forever)") << "\n";

        // 启动时清理过期数据（retentionDays=0 表示永久保留，跳过清理）
        if (cfg.retentionDays > 0) {
            g_dataStore->CleanOldData(cfg.retentionDays);
        }

        // 3. 预置忽略列表 + 开机自启 + IdleDetector 初始配置
        {
            bool configChanged = false;
            std::wstring selfPath = GetSelfPath();
            if (!g_configManager->IsIgnored(selfPath)) {
                g_configManager->AddIgnoredApp(selfPath);
                configChanged = true;
            }
            if (!g_configManager->IsIgnored(L"explorer.exe")) {
                g_configManager->AddIgnoredApp(L"explorer.exe");
                configChanged = true;
            }
            bool regAutoStart = IsAutoStartEnabled();
            if (cfg.autoStart != regAutoStart) {
                SetAutoStartReg(cfg.autoStart);
            }
            if (configChanged) {
                g_configManager->Save(*g_dataStore);
                g_dataStore->Save();
            }
        }

        // 4. 忽略管理器
        g_ignoreManager = new IgnoreManager();
        g_ignoreManager->LoadFrom(g_configManager->GetConfig().ignoreList);

        // 5. 窗口监测
        g_windowMonitor = new WindowMonitor();
        g_windowMonitor->Initialize(hwnd);

        // 6. 空闲检测（初始状态从 ConfigManager 同步）
        g_idleDetector = new IdleDetector();
        g_idleDetector->Initialize(hwnd);
        g_idleDetector->SetTimeout(cfg.idleThresholdMinutes);
        g_idleDetector->SetEnabled(cfg.idleDetectionEnabled);

        // 7. 应用名解析
        g_nameResolver = new AppNameResolver();

        // 8. 计时引擎
        g_engine = new TimerEngine(*g_windowMonitor, *g_idleDetector,
            *g_dataStore, *g_configManager, *g_ignoreManager, *g_nameResolver);

        // 9. 系统托盘
        g_trayManager = new TrayManager();
        g_trayManager->Initialize(hwnd, g_hInst, [hwnd](TrayCommand cmd) {
            WORD wmId = 0;
            switch (cmd) {
            case TrayCommand::OpenPanel:    wmId = IDM_OPEN_PANEL;    break;
            case TrayCommand::TogglePause:  wmId = IDM_TOGGLE_PAUSE;  break;
            case TrayCommand::Settings:     wmId = IDM_SETTINGS;      break;
            case TrayCommand::Exit:         wmId = IDM_EXIT;          break;
            }
            PostMessageW(hwnd, WM_COMMAND, wmId, 0);
        });

        // 10. 轮询定时器
        UINT intervalMs = static_cast<UINT>(cfg.pollingInterval) * 1000;
        SetTimer(hwnd, TIMER_POLLING, intervalMs, nullptr);

        // ---- Phase 4: 创建面板 UI ----
        PanelUI::Initialize(hwnd, g_hInst, hFont);

        // 初始加载今天数据
        if (g_engine) {
            PanelUI::RefreshFull(g_engine->GetTodayData());
        }

        // 启动 UI 刷新定时器（1 秒）
        SetTimer(hwnd, TIMER_REFRESH_UI, 1000, nullptr);

        // 11. 初始状态
        if (cfg.statisticsPaused) g_engine->Pause();
        g_trayManager->SetPausedState(cfg.statisticsPaused || g_engine->IsPaused());
        UpdateTrayTooltip();

        t_cout << "[INIT] Phase 4 - Panel UI ready. TimeTrack is running.\n";
        return 0;
    }

    // ---- 窗口尺寸变化 → 重新布局 ----
    case WM_SIZE:
    {
        int cx = LOWORD(lParam);
        int cy = HIWORD(lParam);
        PanelUI::Resize(cx, cy);
        return 0;
    }

    // ---- 定时器 ----
    case WM_TIMER:
        HandleTimer(wParam);
        return 0;

    // ---- ListView 通知（NM_RCLICK 等） ----
    case WM_NOTIFY:
    {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (nmhdr->idFrom == IDC_LISTVIEW) {
            if (nmhdr->code == NM_RCLICK) {
                // 右键菜单
                std::wstring appPath = PanelUI::GetClickedAppPath(hwnd, lParam);
                if (!appPath.empty()) {
                    // 只对未忽略的应用显示菜单
                    // 注意：ListView 中显示的应用都未被忽略（被忽略的应用不显示）
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, IDM_LV_ADD_IGNORE,
                        L"Add to Ignore List");

                    POINT pt;
                    GetCursorPos(&pt);
                    SetForegroundWindow(hwnd);
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                    PostMessageW(hwnd, WM_NULL, 0, 0);
                    DestroyMenu(hMenu);
                }
            }
            else if (nmhdr->code == NM_DBLCLK) {
                // 双击行：可扩展为显示详情
                std::wstring appPath = PanelUI::GetSelectedAppPath();
                if (!appPath.empty()) {
                    t_cout << "[LV] Double-click: " << ToNarrow(appPath) << "\n";
                }
            }
        }
        return 0;
    }

    // ---- 托盘图标消息 ----
    case WM_APP_TRAYICON:
    {
        if (lParam == WM_LBUTTONUP) {
            // 左键：切换显示/隐藏面板
            if (IsWindowVisible(hwnd)) {
                t_cout << "[TRAY] Left click - hiding panel\n";
                ShowWindow(hwnd, SW_HIDE);
            } else {
                t_cout << "[TRAY] Left click - showing panel\n";
                // 显示前强制加固扩展样式（某些系统操作可能重置 GWL_EXSTYLE）
                SetWindowLongW(hwnd, GWL_EXSTYLE, g_exStyle);
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                // 显示前显式删除任务栏按钮（explorer 可能缓存了旧的窗口状态）
                DeleteTaskbarButton(hwnd);
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
                // 重建图标缓存并重新绑定 ListView（图标数据可能已过时）
                AppIconCache::RebuildCache();
                ListView_SetImageList(PanelUI::hListView, AppIconCache::GetImageList(), LVSIL_SMALL);
                // 确保数据是最新的
                if (!PanelUI::g_isHistoryMode && g_engine) {
                    PanelUI::RefreshFull(g_engine->GetTodayData());
                }
            }
        }
        else if (lParam == WM_RBUTTONUP) {
            if (g_trayManager) {
                bool paused = g_configManager->GetConfig().statisticsPaused
                           || (g_engine ? g_engine->IsPaused() : false);
                g_trayManager->SetPausedState(paused);
                g_trayManager->ShowContextMenu(hwnd);
            }
        }
        return 0;
    }

    // ---- 菜单命令 + 按钮命令 ----
    case WM_COMMAND:
    {
        WORD cmdId = LOWORD(wParam);

        switch (cmdId) {

        // ---- 托盘菜单 ----
        case IDM_OPEN_PANEL:
            t_cout << "[MENU] Open Panel\n";
            // 显示前强制加固扩展样式
            SetWindowLongW(hwnd, GWL_EXSTYLE, g_exStyle);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            // 显示前显式删除任务栏按钮
            DeleteTaskbarButton(hwnd);
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            // 重建图标缓存并重新绑定 ListView
            AppIconCache::RebuildCache();
            ListView_SetImageList(PanelUI::hListView, AppIconCache::GetImageList(), LVSIL_SMALL);
            if (!PanelUI::g_isHistoryMode && g_engine) {
                PanelUI::RefreshFull(g_engine->GetTodayData());
            }
            return 0;

        case IDM_TOGGLE_PAUSE:
        {
            if (!g_engine || !g_configManager) return 0;
            bool wasPaused = g_engine->IsPaused()
                          || g_configManager->GetConfig().statisticsPaused;
            if (wasPaused) {
                g_engine->Resume();
                g_configManager->SetStatisticsPaused(false);
            } else {
                g_engine->Pause();
                g_configManager->SetStatisticsPaused(true);
            }
            g_configManager->Save(*g_dataStore);
            g_dataStore->Save();
            if (g_trayManager) g_trayManager->SetPausedState(!wasPaused);
            UpdateTrayTooltip();
            return 0;
        }

        case IDM_EXIT:
        {
            int result = MessageBoxW(hwnd,
                L"Are you sure you want to exit TimeTrack?\n\nAll tracking data will be saved.",
                L"TimeTrack - Exit Confirmation",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            if (result == IDYES) {
                t_cout << "[EXIT] User confirmed exit\n";
                if (g_engine) g_engine->Flush();
                DestroyWindow(hwnd);
            }
            return 0;
        }

        // ---- 面板按钮 ----
        case IDC_BTN_REFRESH:
            if (!PanelUI::g_isHistoryMode && g_engine) {
                PanelUI::RefreshFull(g_engine->GetTodayData());
            } else {
                NavigateToDate(hwnd, PanelUI::g_currentViewDate);
            }
            return 0;

        case IDC_BTN_TODAY:
            if (PanelUI::g_isHistoryMode) {
                auto now = std::chrono::system_clock::now();
                std::time_t t = std::chrono::system_clock::to_time_t(now);
                std::tm lt;
                localtime_s(&lt, &t);
                char buf[16];
                snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                    lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
                NavigateToDate(hwnd, std::string(buf));
            }
            return 0;

        case IDC_BTN_PREV_DAY:
        {
            std::string date = PanelUI::g_currentViewDate;
            if (date.empty()) return 0;
            IncrementDate(date, -1);
            NavigateToDate(hwnd, date);
            return 0;
        }

        case IDC_BTN_NEXT_DAY:
        {
            std::string date = PanelUI::g_currentViewDate;
            if (date.empty()) return 0;
            IncrementDate(date, 1);
            NavigateToDate(hwnd, date);
            return 0;
        }

        case IDC_BTN_IGNORE:
            ShowIgnoreListDialog(hwnd, g_hInst,
                g_ignoreManager, g_configManager, g_dataStore, g_engine);
            // 刷新面板（忽略列表变化可能影响显示）
            if (!PanelUI::g_isHistoryMode && g_engine) {
                PanelUI::RefreshFull(g_engine->GetTodayData());
            }
            return 0;

        case IDC_BTN_SETTINGS:
        case IDM_SETTINGS:
        {
            ShowSettingsDialog(hwnd, g_hInst, g_configManager, g_dataStore);

            const auto& cfg = g_configManager->GetConfig();

            // Polling interval 改变 → 立即更新定时器
            UINT newInterval = static_cast<UINT>(cfg.pollingInterval) * 1000;
            KillTimer(hwnd, TIMER_POLLING);
            SetTimer(hwnd, TIMER_POLLING, newInterval, nullptr);

            // Idle detection 改变 → 立即更新 IdleDetector
            if (g_idleDetector) {
                g_idleDetector->SetTimeout(cfg.idleThresholdMinutes);
                g_idleDetector->SetEnabled(cfg.idleDetectionEnabled);
            }

            // Auto-start 改变 → 更新注册表
            SetAutoStartReg(cfg.autoStart);

            // Data retention 改变 → 清理过期数据（非 Forever 时）
            if (cfg.retentionDays > 0) {
                g_dataStore->CleanOldData(cfg.retentionDays);
            }

            t_cout << "[SETTINGS] Applied: poll=" << cfg.pollingInterval
                      << "s idle=" << cfg.idleThresholdMinutes
                      << "min idleDetect=" << (cfg.idleDetectionEnabled ? "ON" : "OFF")
                      << " autoStart=" << (cfg.autoStart ? "ON" : "OFF")
                      << " retention=" << cfg.retentionDays << "\n";
            return 0;
        }

        case IDC_BTN_CLOSE:
            // 隐藏窗口到托盘
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        // ---- ListView 右键菜单 ----
        case IDM_LV_ADD_IGNORE:
        {
            std::wstring appPath = PanelUI::GetSelectedAppPath();
            if (!appPath.empty() && g_ignoreManager && g_configManager) {
                g_ignoreManager->Add(appPath);
                g_configManager->AddIgnoredApp(appPath);
                g_configManager->Save(*g_dataStore);
                g_dataStore->Save();

                // 通知引擎
                if (g_engine) {
                    g_engine->Flush();
                }

                // 刷新面板
                if (!PanelUI::g_isHistoryMode && g_engine) {
                    PanelUI::RefreshFull(g_engine->GetTodayData());
                }
                t_cout << "[IGNORE] Added: " << ToNarrow(appPath) << "\n";
            }
            return 0;
        }

        } // end switch(cmdId)
        break;
    }

    // ---- 键盘：ESC 隐藏窗口 ----
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;

    // ---- 窗口关闭 → 隐藏到托盘 ----
    case WM_CLOSE:
        OutputDebugStringW(L"[DEBUG] Intercepted close, hiding instead.\n");
        t_cout << "[WINDOW] Close intercepted, hiding to tray\n";
        ShowWindow(hwnd, SW_HIDE);
        // 隐藏后显式删除任务栏按钮，避免样式残留
        DeleteTaskbarButton(hwnd);
        return 0;

    // ---- 系统关机 ----
    case WM_QUERYENDSESSION:
        t_cout << "[SYSTEM] QueryEndSession - flushing data\n";
        if (g_engine) g_engine->Flush();
        return TRUE;

    case WM_ENDSESSION:
        if (wParam) {
            if (g_engine) g_engine->Flush();
        }
        return 0;

    // ---- 会话变化 ----
    case WM_WTSSESSION_CHANGE:
        if (g_idleDetector) {
            g_idleDetector->OnSessionChange(wParam, false);
        }
        return 0;

    // ---- 窗口销毁 → 完整退出流程 ----
    case WM_DESTROY:
    {
        t_cout << "\n[DESTROY] === Shutdown sequence ===\n";

        // 1. 保存所有计时数据到磁盘
        t_cout << "[DESTROY] Flushing timer data...\n";
        if (g_engine) g_engine->Flush();

        // 2. 移除系统托盘图标
        t_cout << "[DESTROY] Removing tray icon...\n";
        if (g_trayManager) g_trayManager->Remove();

        // 3. 销毁所有定时器
        t_cout << "[DESTROY] Killing timers...\n";
        KillTimer(hwnd, TIMER_POLLING);
        KillTimer(hwnd, TIMER_REFRESH_UI);

        // 4. 清理面板 UI 映射数据
        t_cout << "[DESTROY] Cleaning PanelUI...\n";
        PanelUI::Cleanup();

        // 5. 逐模块释放（逆序：先创建的后释放）
        t_cout << "[DESTROY] Deleting TrayManager...\n";
        delete g_trayManager;   g_trayManager   = nullptr;

        t_cout << "[DESTROY] Deleting TimerEngine...\n";
        delete g_engine;        g_engine        = nullptr;

        t_cout << "[DESTROY] Deleting WindowMonitor...\n";
        delete g_windowMonitor; g_windowMonitor = nullptr;

        t_cout << "[DESTROY] Deleting IdleDetector...\n";
        delete g_idleDetector;  g_idleDetector  = nullptr;

        t_cout << "[DESTROY] Deleting IgnoreManager...\n";
        delete g_ignoreManager; g_ignoreManager = nullptr;

        t_cout << "[DESTROY] Deleting ConfigManager...\n";
        delete g_configManager; g_configManager = nullptr;

        t_cout << "[DESTROY] Deleting DataStore...\n";
        delete g_dataStore;     g_dataStore     = nullptr;

        t_cout << "[DESTROY] Deleting AppNameResolver...\n";
        delete g_nameResolver;  g_nameResolver  = nullptr;

        // 注：SingleInstance mutex 在 WinMain 中 si 析构时自动释放

        t_cout << "[DESTROY] === Cleanup complete. Goodbye! ===\n";
        PostQuitMessage(0);
        return 0;
    }
    case WM_SYSCOMMAND:
        if (wParam == SC_CLOSE) {
            OutputDebugStringW(L"[DEBUG] Intercepted close, hiding instead.\n");
            t_cout << "[WINDOW] SC_CLOSE intercepted, hiding to tray\n";
            ShowWindow(hwnd, SW_HIDE);
            // 隐藏后显式删除任务栏按钮
            DeleteTaskbarButton(hwnd);
            return 0;
        }
        break;
    } // end switch(msg)

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// DeleteTaskbarButton — 通过 ITaskbarList COM 接口显式删除任务栏按钮
// 比单纯操作窗口样式更可靠：直接告知 explorer 该 hwnd 不应有任务栏条目
// ============================================================================
void DeleteTaskbarButton(HWND hwnd) {
    if (!hwnd) return;
    CoInitialize(nullptr);
    ITaskbarList* pTaskbarList = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_ITaskbarList, (void**)&pTaskbarList))) {
        pTaskbarList->HrInit();
        pTaskbarList->DeleteTab(hwnd);
        pTaskbarList->Release();
    }
    CoUninitialize();
}

// ============================================================================
// WinMain — 程序入口
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    g_hInst = hInstance;

    // ---- 命令行参数解析：--console 启用控制台调试输出 ----
    {
        std::string cmdStr(lpCmdLine);
        if (cmdStr.find("--console") != std::string::npos) {
            g_consoleEnabled = true;
        }
    }

    // ---- 控制台分配（仅 --console 模式下启用，避免任务栏多余条目） ----
    if (g_consoleEnabled) {
        BOOL ok = AllocConsole();
        if (ok) {
            FILE* fDummy;
            freopen_s(&fDummy, "CONOUT$", "w", stdout);
            freopen_s(&fDummy, "CONOUT$", "w", stderr);
            freopen_s(&fDummy, "CONIN$",  "r", stdin);
            SetConsoleOutputCP(CP_UTF8);
        }
    }

    // ---- 初始化 Common Controls（ListView/Trackbar 等需要） ----
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icex);

    // ---- 单实例检查 ----
    SingleInstance si;
    if (!si.TryAcquire(L"Local\\TimeTrack_SingleInstance")) {
        OutputDebugStringW(L"[FATAL] Failed to create singleton mutex\n");
        return 1;
    }
    if (!si.IsFirstInstance()) {
        return 0;
    }

    t_cout << "============================================\n";
    t_cout << "  TimeTrack v1.0 - Phase 5 (Final)\n";
    t_cout << "  Full integration: Panel UI + Tray +\n";
    t_cout << "  Settings (live apply) + Data retention\n";
    t_cout << "============================================\n\n";

    // ---- 注册窗口类 ----
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_TIMETRACK));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = MAIN_WINDOW_CLASS;
    wc.hIconSm       = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_TIMETRACK));

    if (!RegisterClassExW(&wc)) {
        OutputDebugStringW(L"[FATAL] Failed to register window class\n");
        return 1;
    }

    // ---- 创建主窗口 ----
    // WS_EX_TOOLWINDOW: 窗口不出现在任务栏和 Alt+Tab 中（仅托盘可见）
    // WS_POPUP 替代 WS_OVERLAPPED: 避免 Overlapped 窗口被 explorer 自动标记为应用主窗口
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        MAIN_WINDOW_CLASS,
        L"TimeTrack",
        WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        650, 500,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        OutputDebugStringW(L"[FATAL] Failed to create main window\n");
        return 1;
    }

    // 强制剥离所有可能产生任务栏条目的样式位
    // WS_EX_APPWINDOW:   某些 DPI/主题场景下系统可能隐式设置（explorer 据此显示任务栏按钮）
    // WS_EX_WINDOWEDGE:  3D 边框样式，某些场景触发任务栏策略
    // WS_EX_TOOLWINDOW:  工具窗口标识（不在任务栏/Alt+Tab 中显示）
    g_exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    g_exStyle &= ~(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE);
    g_exStyle |= WS_EX_TOOLWINDOW;
    SetWindowLongW(hwnd, GWL_EXSTYLE, g_exStyle);

    // SWP_FRAMECHANGED:  强制系统重新评估窗口边框/任务栏策略（立即生效）
    // SWP_HIDEWINDOW:    确保初始状态为隐藏
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_HIDEWINDOW);

    // 初始状态：隐藏（后台运行，托盘可见）
    ShowWindow(hwnd, SW_HIDE);
    // ITaskbarList::DeleteTab — 彻底清除 explorer 可能残留的任务栏按钮
    DeleteTaskbarButton(hwnd);
    UpdateWindow(hwnd);

    // ---- 消息泵 ----
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 仅控制台模式下释放控制台
    if (g_consoleEnabled) {
        FreeConsole();
    }

    return static_cast<int>(msg.wParam);
}
