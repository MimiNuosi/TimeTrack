// TimeTrack - 前台窗口监测实现
// 使用 GetForegroundWindow / GetWindowText / GetWindowThreadProcessId 提取窗口信息
// 通过 SetWinEventHook(EVENT_SYSTEM_FOREGROUND) 实现事件驱动，零 CPU 轮询
// 诊断日志：首次遇到每个 PID 时输出完整窗口信息
// UWP支持：检测 ApplicationFrameHost.exe 托管的子进程获取真实应用路径
#include "WindowMonitor.h"
#include <psapi.h>   // GetModuleFileNameExW / GetProcessImageFileNameW
#include <set>

// 链接 psapi
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "user32.lib")

// ---- 诊断：已见过的 PID 集合，避免重复输出 ----
static std::set<DWORD> g_diagSeenPIDs;

// ---- 诊断辅助：将 uintptr_t 格式化为大写的十六进制字符串 ----
static std::wstring FormatHex(uintptr_t val) {
    if (val == 0) return L"0";
    wchar_t tmp[20];
    int pos = 19;
    tmp[19] = L'\0';
    while (val > 0 && pos > 0) {
        tmp[--pos] = L"0123456789ABCDEF"[val & 0xF];
        val >>= 4;
    }
    return std::wstring(&tmp[pos]);
}

// ---- 诊断辅助：输出完整的窗口诊断信息 ----
static void DiagWindowInfo(HWND hwnd, const wchar_t* className, const std::wstring& title,
                           DWORD pid, const std::wstring& path) {
    std::wstring msg = L"[WinMon] HWND=0x";
    msg += FormatHex(reinterpret_cast<uintptr_t>(hwnd));
    msg += L" class='";
    msg += className;
    msg += L"' title='";
    msg += title;
    msg += L"' PID=";
    msg += std::to_wstring(pid);
    msg += L" path='";
    msg += path;
    msg += L"'\n";
    OutputDebugStringW(msg.c_str());
}

// ---- UWP 子窗口枚举回调：查找 Windows.UI.Core.CoreWindow ----
struct UWPSearchContext {
    HWND  foundHwnd;
    DWORD parentPid;
};

static BOOL CALLBACK EnumUWPChildProc(HWND hwndChild, LPARAM lParam) {
    auto* ctx = reinterpret_cast<UWPSearchContext*>(lParam);
    if (ctx->foundHwnd) return FALSE; // 已找到，停止枚举

    // 检查子窗口所属进程是否不同于父进程
    DWORD childPid = 0;
    GetWindowThreadProcessId(hwndChild, &childPid);
    if (childPid == 0 || childPid == ctx->parentPid) {
        return TRUE; // 同进程的子窗口不关心，继续枚举
    }

    // 获取类名并检查是否是 CoreWindow
    wchar_t className[256] = {};
    GetClassNameW(hwndChild, className, _countof(className));
    if (wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0) {
        ctx->foundHwnd = hwndChild;
        return FALSE; // 找到目标，停止枚举
    }

    return TRUE; // 继续枚举
}

// ---- 从进程PID获取完整exe路径（独立函数，复用） ----
static bool GetProcessPath(DWORD pid, wchar_t* outPath, DWORD outSize) {
    // 尝试完整权限（PROCESS_QUERY_INFORMATION + PROCESS_VM_READ）
    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

    // 如果完整权限失败（如系统/提权进程），尝试降级权限
    // PROCESS_QUERY_LIMITED_INFORMATION 在 Vista+ 上可用于 QueryFullProcessImageNameW
    if (!hProcess) {
        hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    }
    if (!hProcess) return false;

    bool ok = false;
    DWORD pathLen = outSize;
    if (QueryFullProcessImageNameW(hProcess, 0, outPath, &pathLen)) {
        ok = true;
    } else if (GetModuleFileNameExW(hProcess, nullptr, outPath, outSize)) {
        ok = true;
    }
    CloseHandle(hProcess);
    return ok;
}

// ============================================================================
// 构造与析构
// ============================================================================

WindowMonitor::~WindowMonitor() {
    if (m_hEventHook) {
        UnhookWinEvent(m_hEventHook);
        m_hEventHook = nullptr;
    }
}

// ============================================================================
// Initialize — 注册 WinEventHook
// ============================================================================

bool WindowMonitor::Initialize(HWND hwndMsg, WindowChangeCallback onChanged) {
    m_onChanged = std::move(onChanged);

    if (hwndMsg) {
        m_hEventHook = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
            nullptr, WinEventProc,
            0, 0,
            WINEVENT_OUTOFCONTEXT);
    }

    Refresh();
    return true;
}

// ============================================================================
// Refresh — 主动轮询当前前台窗口
// ============================================================================

void WindowMonitor::Refresh() {
    HWND fg = GetForegroundWindow();
    if (!fg) {
        m_current = WindowInfo{};
        return;
    }
    OnForegroundChanged(fg);
}

// ============================================================================
// BuildWindowInfo — 从 HWND 提取完整信息（含诊断日志 + UWP子进程解析）
// ============================================================================

WindowInfo WindowMonitor::BuildWindowInfo(HWND hwnd) {
    WindowInfo info;
    info.hwnd = hwnd;

    // 1) 获取进程 ID
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    info.processId = pid;

    // 2) 获取窗口标题
    wchar_t title[512] = {};
    int titleLen = GetWindowTextW(hwnd, title, _countof(title));
    info.windowTitle = (titleLen > 0) ? title : L"";

    // 3) 获取进程路径
    {
        wchar_t path[MAX_PATH] = {};
        if (GetProcessPath(pid, path, MAX_PATH)) {
            info.processPath = path;
        }
    }

    // ---- UWP 特殊处理：ApplicationFrameHost.exe 托管窗口 ----
    // ApplicationFrameHost.exe 是 UWP 应用的宿主进程，
    // 其窗口内嵌了真实 UWP 应用的 CoreWindow 子窗口。
    // 需要提取子进程的真实 exe 路径以正确显示图标和名称。
    {
        bool isUWP = false;
        if (!info.processPath.empty()) {
            // 不区分大小写检查路径是否以 \ApplicationFrameHost.exe 结尾
            size_t pos = info.processPath.rfind(L'\\');
            if (pos != std::wstring::npos) {
                std::wstring fileName = info.processPath.substr(pos + 1);
                if (_wcsicmp(fileName.c_str(), L"ApplicationFrameHost.exe") == 0) {
                    isUWP = true;
                }
            }
        }

        if (isUWP) {
            // 枚举子窗口找到 CoreWindow
            UWPSearchContext ctx = {};
            ctx.parentPid = pid;
            EnumChildWindows(hwnd, EnumUWPChildProc, reinterpret_cast<LPARAM>(&ctx));

            if (ctx.foundHwnd) {
                DWORD childPid = 0;
                GetWindowThreadProcessId(ctx.foundHwnd, &childPid);
                if (childPid != 0 && childPid != pid) {
                    wchar_t childPath[MAX_PATH] = {};
                    if (GetProcessPath(childPid, childPath, MAX_PATH)) {
                        info.processPath = childPath;
                        info.processId = childPid;
                    }
                }
            }
        }
    }

    // 4) 判断最小化状态
    info.isMinimized = (IsIconic(hwnd) != 0);

    // 5) 判断是否为前台窗口
    info.isForeground = (hwnd == GetForegroundWindow());

    // ---- 诊断日志 ----
    {
        wchar_t className[256] = {};
        GetClassNameW(hwnd, className, _countof(className));

        bool bothEmpty = info.processPath.empty() && info.windowTitle.empty();

        if (bothEmpty) {
            std::wstring warn = L"[WINDOW EMPTY] HWND=0x";
            warn += FormatHex(reinterpret_cast<uintptr_t>(hwnd));
            warn += L" class='";
            warn += className;
            warn += L"'\n";
            OutputDebugStringW(warn.c_str());
        } else {
            if (g_diagSeenPIDs.insert(pid).second) {
                DiagWindowInfo(hwnd, className, info.windowTitle, pid, info.processPath);
            }
        }
    }

    return info;
}

// ============================================================================
// OnForegroundChanged — 窗口切换时更新内部状态
// ============================================================================

void WindowMonitor::OnForegroundChanged(HWND hwndNew) {
    WindowInfo newInfo = BuildWindowInfo(hwndNew);

    if (!hwndNew || !IsWindow(hwndNew)) {
        return;
    }

    WindowInfo oldInfo = m_current;
    m_current = std::move(newInfo);

    if (m_onChanged) {
        m_onChanged(oldInfo, m_current);
    }
}

// ============================================================================
// WinEventProc — 静态回调
// ============================================================================

void CALLBACK WindowMonitor::WinEventProc(
    HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
    LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    if (!hwnd || !IsWindow(hwnd)) return;

    (void)hWinEventHook;
    (void)event;
    (void)dwEventThread;
    (void)dwmsEventTime;
}
