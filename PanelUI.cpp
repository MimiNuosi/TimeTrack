// TimeTrack - 统计面板 UI 实现
// 所有子控件创建、布局缩放、ListView 数据刷新

#include "PanelUI.h"
#include "AppIconCache.h"
#include "Resource.h"
#include <string>
#include <algorithm>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

// 链接 Common Controls 库
#pragma comment(lib, "comctl32.lib")

namespace PanelUI {

// ============================================================================
// 全局状态
// ============================================================================

HWND hHeader       = nullptr;
HWND hBtnPrevDay   = nullptr;
HWND hBtnNextDay   = nullptr;
HWND hBtnToday     = nullptr;
HWND hListView     = nullptr;
HWND hBtnRefresh   = nullptr;
HWND hBtnIgnore    = nullptr;
HWND hBtnSettings  = nullptr;
HWND hBtnClose     = nullptr;

bool                   g_isHistoryMode    = false;
std::string           g_currentViewDate;
int                    g_lastActiveIndex  = -1;
std::map<std::wstring, int> g_appToIndex;
std::vector<std::wstring>   g_displayedAppPaths;

// 字体常量（预留多语言切换）
static const wchar_t* FONT_FACE     = L"Segoe UI";
static const wchar_t* BTN_PREV      = L"\u25C0";  // ◀
static const wchar_t* BTN_NEXT      = L"\u25B6";  // ▶
static const wchar_t* BTN_TODAY     = L"Today";
static const wchar_t* BTN_REFRESH   = L"Refresh";
static const wchar_t* BTN_IGNORE    = L"Ignored Apps...";
static const wchar_t* BTN_SETTINGS  = L"Settings...";
static const wchar_t* BTN_CLOSE     = L"Close";
static const wchar_t* LV_COL_APP    = L"Application";
static const wchar_t* LV_COL_DUR    = L"Duration";
static const wchar_t* LV_COL_PCT    = L"%";
static const wchar_t* LV_COL_STATUS = L"Status";

// ListView 列宽常量
static const int COL_DUR_WIDTH   = 100;
static const int COL_PCT_WIDTH   = 55;
static const int COL_STATUS_WIDTH = 100;
static const int COL_APP_MIN_WIDTH = 120;

// 按钮尺寸常量
static const int BTN_W = 100;
static const int BTN_H = 28;
static const int MARGIN = 5;
static const int HEADER_H = 28;
static const int NAV_BTN_W = 30;
static const int TODAY_BTN_W = 55;
static const int BAR_H = 36;

// ============================================================================
// 辅助：WCHAR 版本的安全格式化
// ============================================================================

static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], len);
    return ws;
}

static std::string ToNarrow(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], len, nullptr, nullptr);
    return s;
}

// 补零辅助：个位数前加 "0"
static std::wstring Pad2W(uint64_t v) {
    return (v < 10) ? (L"0" + std::to_wstring(v)) : std::to_wstring(v);
}

// 秒数 → "HH:MM:SS"（纯 wstring，零栈缓冲区）
static std::wstring FormatDurationW(uint64_t totalSec) {
    uint64_t h = totalSec / 3600;
    uint64_t m = (totalSec % 3600) / 60;
    uint64_t s = totalSec % 60;
    return Pad2W(h) + L":" + Pad2W(m) + L":" + Pad2W(s);
}

// 百分比格式化（纯 wstring，零栈缓冲区，手动 1 位小数）
static std::wstring FormatPercentW(uint64_t seconds, uint64_t totalSeconds) {
    if (totalSeconds == 0) return L"0%";
    double pct = (static_cast<double>(seconds) / totalSeconds) * 100.0;
    if (pct < 0.1 && seconds > 0) {
        return L"<1%";
    } else if (pct < 10.0) {
        // 四舍五入到 1 位小数（如 3.57 → "3.6%"）
        int pct10 = static_cast<int>(pct * 10.0 + 0.5);
        return std::to_wstring(pct10 / 10) + L"." + std::to_wstring(pct10 % 10) + L"%";
    } else {
        return std::to_wstring(static_cast<int>(pct + 0.5)) + L"%";
    }
}

// ============================================================================
// Initialize — 创建所有子控件
// ============================================================================

void Initialize(HWND hwndParent, HINSTANCE hInst, HFONT hFont) {
    // ---- 共享样式：子窗口 + 可见（除 ListView 外都加上 WS_TABSTOP） ----
    DWORD ctrlStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP;

    // ---- 头部区域 ----
    // 日期 + 总时长静态文本（左对齐，可接收点击用作日期选择）
    hHeader = CreateWindowExW(0, L"STATIC", L"TimeTrack",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY | SS_CENTERIMAGE,
        MARGIN, MARGIN, 300, HEADER_H,
        hwndParent, (HMENU)IDC_HEADER, hInst, nullptr);

    // ◀ 前一天按钮
    hBtnPrevDay = CreateWindowExW(0, L"BUTTON", BTN_PREV,
        ctrlStyle, 0, 0, NAV_BTN_W, BTN_H,
        hwndParent, (HMENU)IDC_BTN_PREV_DAY, hInst, nullptr);

    // ▶ 后一天按钮
    hBtnNextDay = CreateWindowExW(0, L"BUTTON", BTN_NEXT,
        ctrlStyle, 0, 0, NAV_BTN_W, BTN_H,
        hwndParent, (HMENU)IDC_BTN_NEXT_DAY, hInst, nullptr);

    // [Today] 按钮
    hBtnToday = CreateWindowExW(0, L"BUTTON", BTN_TODAY,
        ctrlStyle, 0, 0, TODAY_BTN_W, BTN_H,
        hwndParent, (HMENU)IDC_BTN_TODAY, hInst, nullptr);

    // ---- 主 ListView（报告视图 + 整行选择 + 网格线） ----
    // LVS_REPORT:          报告视图（列标题 + 行）
    // LVS_SINGLESEL:       单选
    // LVS_SHOWSELALWAYS:   即使失焦也显示选中
    // LVS_EX_FULLROWSELECT: 整行选中（扩展样式）
    hListView = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 400, 200,
        hwndParent, (HMENU)IDC_LISTVIEW, hInst, nullptr);

    // 设置扩展样式：整行选中
    ListView_SetExtendedListViewStyleEx(hListView,
        LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

    // 添加列
    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt = LVCFMT_LEFT;

    lvc.pszText = const_cast<LPWSTR>(LV_COL_APP);
    lvc.cx = COL_APP_MIN_WIDTH;
    ListView_InsertColumn(hListView, 0, &lvc);

    lvc.fmt = LVCFMT_RIGHT;
    lvc.pszText = const_cast<LPWSTR>(LV_COL_DUR);
    lvc.cx = COL_DUR_WIDTH;
    ListView_InsertColumn(hListView, 1, &lvc);

    lvc.pszText = const_cast<LPWSTR>(LV_COL_PCT);
    lvc.cx = COL_PCT_WIDTH;
    ListView_InsertColumn(hListView, 2, &lvc);

    lvc.fmt = LVCFMT_LEFT;
    lvc.pszText = const_cast<LPWSTR>(LV_COL_STATUS);
    lvc.cx = COL_STATUS_WIDTH;
    ListView_InsertColumn(hListView, 3, &lvc);

    // ---- 图标支持：创建 ImageList 并绑定到 ListView ----
    // 16×16 小图标（LVSIL_SMALL），32 位真彩色，初始容量 64
    AppIconCache::Init();
    ListView_SetImageList(hListView, AppIconCache::GetImageList(), LVSIL_SMALL);

    // ---- 底部按钮栏 ----
    hBtnRefresh = CreateWindowExW(0, L"BUTTON", BTN_REFRESH,
        ctrlStyle, 0, 0, BTN_W, BTN_H,
        hwndParent, (HMENU)IDC_BTN_REFRESH, hInst, nullptr);

    hBtnIgnore = CreateWindowExW(0, L"BUTTON", BTN_IGNORE,
        ctrlStyle, 0, 0, BTN_W + 30, BTN_H,
        hwndParent, (HMENU)IDC_BTN_IGNORE, hInst, nullptr);

    hBtnSettings = CreateWindowExW(0, L"BUTTON", BTN_SETTINGS,
        ctrlStyle, 0, 0, BTN_W, BTN_H,
        hwndParent, (HMENU)IDC_BTN_SETTINGS, hInst, nullptr);

    hBtnClose = CreateWindowExW(0, L"BUTTON", BTN_CLOSE,
        ctrlStyle, 0, 0, BTN_W, BTN_H,
        hwndParent, (HMENU)IDC_BTN_CLOSE, hInst, nullptr);

    // ---- 应用字体 ----
    if (hFont) {
        SendMessageW(hHeader,       WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hBtnPrevDay,   WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hBtnNextDay,   WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hBtnToday,     WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hListView,     WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hBtnRefresh,   WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hBtnIgnore,    WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hBtnSettings,  WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hBtnClose,     WM_SETFONT, (WPARAM)hFont, TRUE);
    }
}

// ============================================================================
// Resize — 响应 WM_SIZE 重新布局所有控件
// ============================================================================

void Resize(int cx, int cy) {
    if (cx <= 0 || cy <= 0) return;

    // ---- 头部区域布局 ----
    // ◀ 按钮：右侧
    int xRight = cx - MARGIN;
    SetWindowPos(hBtnNextDay, nullptr,
        xRight - NAV_BTN_W, MARGIN, NAV_BTN_W, BTN_H, SWP_NOZORDER);
    xRight -= NAV_BTN_W;

    SetWindowPos(hBtnPrevDay, nullptr,
        xRight - NAV_BTN_W, MARGIN, NAV_BTN_W, BTN_H, SWP_NOZORDER);
    xRight -= NAV_BTN_W + MARGIN;

    // [Today] 按钮
    SetWindowPos(hBtnToday, nullptr,
        xRight - TODAY_BTN_W, MARGIN, TODAY_BTN_W, BTN_H, SWP_NOZORDER);
    xRight -= TODAY_BTN_W + MARGIN;
    SetWindowPos(hBtnToday, nullptr,
        xRight + 1, MARGIN, TODAY_BTN_W, BTN_H, SWP_NOZORDER);

    // 头部标签占剩余宽度
    int headerRight = xRight - MARGIN;
    int headerW = headerRight - MARGIN;
    SetWindowPos(hHeader, nullptr, MARGIN, MARGIN, headerW, HEADER_H, SWP_NOZORDER);

    // ---- 底部按钮栏布局 ----
    int barY = cy - BAR_H;
    int barSpace = cx - 2 * MARGIN;
    int totalBtnW = BTN_W * 4 + 30; // refresh + ignore(wider) + settings + close
    int btnX = (barSpace - totalBtnW) / 2 + MARGIN;

    SetWindowPos(hBtnRefresh,  nullptr, btnX, barY + 4, BTN_W, BTN_H, SWP_NOZORDER);
    btnX += BTN_W + 8;
    SetWindowPos(hBtnIgnore,   nullptr, btnX, barY + 4, BTN_W + 30, BTN_H, SWP_NOZORDER);
    btnX += BTN_W + 30 + 8;
    SetWindowPos(hBtnSettings, nullptr, btnX, barY + 4, BTN_W, BTN_H, SWP_NOZORDER);
    btnX += BTN_W + 8;
    SetWindowPos(hBtnClose,    nullptr, btnX, barY + 4, BTN_W, BTN_H, SWP_NOZORDER);

    // ---- ListView 填充剩余空间 ----
    int lvY = HEADER_H + MARGIN * 2;
    int lvH = barY - lvY - MARGIN;
    SetWindowPos(hListView, nullptr, MARGIN, lvY, cx - 2 * MARGIN, lvH, SWP_NOZORDER);

    // Adjust "Application" column width to fill remaining space
    int lvTotalW = cx - 2 * MARGIN;
    int appColW = lvTotalW - COL_DUR_WIDTH - COL_PCT_WIDTH - COL_STATUS_WIDTH - 4; // 4 for scrollbar margin
    if (appColW < COL_APP_MIN_WIDTH) appColW = COL_APP_MIN_WIDTH;
    ListView_SetColumnWidth(hListView, 0, appColW);
}

// ============================================================================
// UpdateHeader — 更新头部日期和总时长
// ============================================================================

void UpdateHeader(const std::string& date, uint64_t totalSeconds) {
    g_currentViewDate = date;

    std::wstring wDate = ToWide(date);
    std::wstring text = wDate + L"  |  " + FormatHeaderTotal(totalSeconds);

    if (g_isHistoryMode) {
        text += L"  (history)";
    }

    SetWindowTextW(hHeader, text.c_str());
}

// 头部总时长（纯 wstring，零栈缓冲区）
std::wstring FormatHeaderTotal(uint64_t totalSec) {
    uint64_t h = totalSec / 3600;
    uint64_t m = (totalSec % 3600) / 60;
    return L"Total: " + std::to_wstring(h) + L"h " + std::to_wstring(m) + L"m";
}

// ============================================================================
// LvSetItemText — 安全的 ListView 单元格写入（LVITEMW 零初始化）
// 替代 ListView_SetItemText 宏（其未初始化 LV_ITEM 可能导致栈越界）
// ============================================================================
static void LvSetItemText(HWND hLV, int item, int subItem, LPCWSTR text) {
    LVITEMW lvi = {};  // 零初始化所有字段（消除未定义行为）
    lvi.iSubItem = subItem;
    lvi.pszText = const_cast<LPWSTR>(text);
    SendMessageW(hLV, LVM_SETITEMTEXT, (WPARAM)item, (LPARAM)&lvi);
}

// ============================================================================
// RefreshIncremental — 增量刷新（每秒调用）
// ============================================================================

void RefreshIncremental(const DailyData& today,
                         const std::wstring& currentApp,
                         const std::wstring& displayName)
{
    // 更新头部
    UpdateHeader(today.date, today.totalSeconds);

    if (today.entries.empty() && g_displayedAppPaths.empty()) {
        return; // 无数据，无需操作
    }

    // 检查条目数量是否变化
    if (today.entries.size() != g_displayedAppPaths.size()) {
        // 应用列表变化 → 全量重建
        RefreshFull(today);
        return;
    }

    // 增量更新：更新当前应用的 Duration / Percent 列 + Status 列
    int currentIdx = -1;

    // 清除旧 [ACTIVE] 状态
    if (g_lastActiveIndex >= 0 && g_lastActiveIndex < (int)g_displayedAppPaths.size()) {
        LvSetItemText(hListView, g_lastActiveIndex, 3, L"");
    }

    // 找到当前应用的行并更新
    for (size_t i = 0; i < g_displayedAppPaths.size(); ++i) {
        bool isCurrent = (_wcsicmp(g_displayedAppPaths[i].c_str(), currentApp.c_str()) == 0);
        if (isCurrent) {
            currentIdx = (int)i;
            // 在 today.entries 中查找该应用的秒数
            for (const auto& entry : today.entries) {
                if (_wcsicmp(entry.appPath.c_str(), currentApp.c_str()) == 0) {
                    // 更新 Duration 列（按秒数降序，序号不变所以直接更新行）
                    std::wstring dur = FormatDurationW(entry.seconds);
                    LvSetItemText(hListView, currentIdx, 1, dur.c_str());

                    // 更新 Percent 列
                    std::wstring pct = FormatPercentW(entry.seconds, today.totalSeconds);
                    LvSetItemText(hListView, currentIdx, 2, pct.c_str());

                    break;
                }
            }
            break;
        }
    }

    // 设置新 [ACTIVE] 状态
    if (currentIdx >= 0) {
        LvSetItemText(hListView, currentIdx, 3, L"[ACTIVE]");
        g_lastActiveIndex = currentIdx;
    } else {
        g_lastActiveIndex = -1;
    }

    // 更新 appToIndex 映射（条目可能因排序而位置变化）
    g_appToIndex.clear();
    for (size_t i = 0; i < g_displayedAppPaths.size(); ++i) {
        g_appToIndex[g_displayedAppPaths[i]] = (int)i;
    }
}

// ============================================================================
// RefreshFull — 全量重建 ListView
// ============================================================================

void RefreshFull(const DailyData& data) {
    // 清除旧数据
    ListView_DeleteAllItems(hListView);
    g_appToIndex.clear();
    g_displayedAppPaths.clear();
    g_lastActiveIndex = -1;

    // 更新头部
    UpdateHeader(data.date, data.totalSeconds);

    if (data.entries.empty()) {
        return;
    }

    // 按秒数降序排列
    std::vector<AppTimeEntry> sorted = data.entries;
    std::sort(sorted.begin(), sorted.end(),
        [](const AppTimeEntry& a, const AppTimeEntry& b) {
            return a.seconds > b.seconds;
        });

    // 添加到 ListView
    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& entry = sorted[i];

        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_IMAGE;
        item.iItem = (int)i;
        item.pszText = const_cast<LPWSTR>(entry.displayName.c_str());
        item.iImage = AppIconCache::GetOrLoadIcon(entry.appPath);

        // 如果显示名称为空，使用文件基本名
        std::wstring fallbackName;
        if (entry.displayName.empty()) {
            size_t pos = entry.appPath.find_last_of(L"\\/");
            fallbackName = (pos != std::wstring::npos)
                ? entry.appPath.substr(pos + 1) : entry.appPath;
            item.pszText = const_cast<LPWSTR>(fallbackName.c_str());
        }

        int idx = ListView_InsertItem(hListView, &item);
        if (idx >= 0) {
            // Duration
            std::wstring dur = FormatDurationW(entry.seconds);
            LvSetItemText(hListView, idx, 1, dur.c_str());

            // Percent
            std::wstring pct = FormatPercentW(entry.seconds, data.totalSeconds);
            LvSetItemText(hListView, idx, 2, pct.c_str());

            // Status（初始为空）
            LvSetItemText(hListView, idx, 3, L"");
        }

        g_appToIndex[entry.appPath] = idx;
        g_displayedAppPaths.push_back(entry.appPath);
    }

    // 恢复历史模式标记
    if (g_isHistoryMode) {
        UpdateHeader(data.date, data.totalSeconds);
    }
}

// ============================================================================
// LoadHistoryData — 加载历史日期数据
// ============================================================================

void LoadHistoryData(const std::string& date, const DailyData& data) {
    g_isHistoryMode = true;
    g_currentViewDate = date;
    RefreshFull(data);
}

// ============================================================================
// SetHistoryMode — 切换历史/实时模式
// ============================================================================

void SetHistoryMode(bool isHistory) {
    g_isHistoryMode = isHistory;
    g_lastActiveIndex = -1;
}

// ============================================================================
// GetClickedAppPath — ListView 右键定位
// ============================================================================

std::wstring GetClickedAppPath(HWND hwnd, LPARAM lParam) {
    // NM_RCLICK 的 lParam 指向 NMITEMACTIVATE 结构
    NMITEMACTIVATE* nmia = (NMITEMACTIVATE*)lParam;
    int idx = nmia->iItem;

    if (idx < 0 || idx >= (int)g_displayedAppPaths.size()) {
        // 没有命中行 → 使用 LVM_SUBITEMHITTEST 尝试
        LVHITTESTINFO ht = {};
        GetCursorPos(&ht.pt);
        ScreenToClient(hListView, &ht.pt);
        idx = ListView_HitTest(hListView, &ht);
        if (idx < 0 || idx >= (int)g_displayedAppPaths.size()) {
            return L"";
        }
    }

    // 选中被点击的行
    ListView_SetItemState(hListView, idx, LVIS_SELECTED | LVIS_FOCUSED,
        LVIS_SELECTED | LVIS_FOCUSED);

    return g_displayedAppPaths[idx];
}

// ============================================================================
// GetSelectedAppPath — 获取当前选中行
// ============================================================================

std::wstring GetSelectedAppPath() {
    int idx = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
    if (idx < 0 || idx >= (int)g_displayedAppPaths.size()) {
        return L"";
    }
    return g_displayedAppPaths[idx];
}

// ============================================================================
// Cleanup — 清理资源
// ============================================================================

void Cleanup() {
    // 销毁图标 ImageList（释放所有托管图标）
    AppIconCache::Cleanup();

    // 所有句柄由父窗口 DestroyWindow 自动销毁，此处无需额外释放
    // 仅清除映射数据
    g_appToIndex.clear();
    g_displayedAppPaths.clear();
    g_lastActiveIndex = -1;
}

} // namespace PanelUI
