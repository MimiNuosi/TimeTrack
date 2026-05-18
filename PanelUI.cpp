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

#include <d2d1.h>
#include <dwrite.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

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

// ============================================================================
// DirectWrite 文字渲染支持
// ============================================================================

// COM 安全释放宏
#define SAFE_RELEASE_DW(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while(0)

// DirectWrite 全局对象
ID2D1Factory*       g_pD2DFactory         = nullptr;
IDWriteFactory*     g_pDWriteFactory      = nullptr;
IDWriteTextFormat*  g_pTextFormatHeader   = nullptr;
IDWriteTextFormat*  g_pTextFormatNormal   = nullptr;
IDWriteTextFormat*  g_pTextFormatItem     = nullptr; // Segoe UI Variable 10pt

// 缓存的列表条目绘制数据（与 g_displayedAppPaths 一一对应）
std::vector<ListItemDisplay> g_listItemDisplay;

// 子类化原始窗口过程指针
static WNDPROC g_pfnOrigHeaderProc  = nullptr;
static WNDPROC g_pfnOrigListViewProc = nullptr;

// 缓存头部文本（供 DirectWrite 绘制使用）
static std::wstring g_sHeaderText;

// 子类化窗口过程的前向声明（在 Initialize 之后定义）
static LRESULT CALLBACK HeaderSubclassProc(HWND hwnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK ListViewSubclassProc(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam);

// 按钮子类化状态（每个按钮独立跟踪 hover/原始窗口过程）
static std::map<HWND, WNDPROC> g_btnOrigProc;
static std::map<HWND, bool>    g_btnHover;
static std::map<HWND, bool>    g_btnTracking;

// DWrite 字号（通过 CreateTextFormat 传递 DIP 值, 1 DIP = 1/96 inch）
static const FLOAT HEADER_FONT_SIZE_DIP = 16.0f;   // 等价于 12pt
static const FLOAT NORMAL_FONT_SIZE_DIP = 12.0f;   // 等价于 9pt
static const FLOAT ITEM_FONT_SIZE_DIP   = 13.333f; // 等价于 10pt (10×96/72)

// 浅色主题颜色常量
static const COLORREF CLR_LIST_BG    = RGB(255, 255, 255);  // ListView 背景白
static const COLORREF CLR_LIST_TEXT_BG = RGB(255, 255, 255);  // ListView 文字背景白

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

// 图标尺寸
static const int ICON_SIZE = 20;  // Win11 图标 20×20 像素
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

void Initialize(HWND hwndParent, HINSTANCE hInst,
                HFONT hFontHeader, HFONT hFontNormal)
{
    // ---- 共享样式：子窗口 + 可见 + 扁平边框（除 ListView 外都加上 WS_TABSTOP） ----
    DWORD ctrlStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_FLAT;

    // 按钮样式追加 BS_FLAT 的便捷 lambda
    auto MakeFlat = [](HWND hBtn) {
        LONG_PTR style = GetWindowLongPtrW(hBtn, GWL_STYLE);
        style |= BS_FLAT;
        SetWindowLongPtrW(hBtn, GWL_STYLE, style);
    };

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

    // ---- 主 ListView（报告视图 + 整行选择 + 网格线 + 双缓冲） ----
    // LVS_REPORT:          报告视图（列标题 + 行）
    // LVS_SINGLESEL:       单选
    // LVS_SHOWSELALWAYS:   即使失焦也显示选中
    // LVS_EX_FULLROWSELECT: 整行选中（扩展样式）
    // LVS_EX_GRIDLINES:     网格线
    // LVS_EX_DOUBLEBUFFER:  消除刷新闪烁
    hListView = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 400, 200,
        hwndParent, (HMENU)IDC_LISTVIEW, hInst, nullptr);

    // 设置扩展样式：整行选中 + 网格线 + 双缓冲
    ListView_SetExtendedListViewStyleEx(hListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // 选中行颜色：蓝底白字（背景色由系统自动处理）
    ListView_SetBkColor(hListView, CLR_LIST_BG);
    ListView_SetTextBkColor(hListView, CLR_LIST_TEXT_BG);

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
    MakeFlat(hBtnRefresh);

    hBtnIgnore = CreateWindowExW(0, L"BUTTON", BTN_IGNORE,
        ctrlStyle, 0, 0, BTN_W + 30, BTN_H,
        hwndParent, (HMENU)IDC_BTN_IGNORE, hInst, nullptr);
    MakeFlat(hBtnIgnore);

    hBtnSettings = CreateWindowExW(0, L"BUTTON", BTN_SETTINGS,
        ctrlStyle, 0, 0, BTN_W, BTN_H,
        hwndParent, (HMENU)IDC_BTN_SETTINGS, hInst, nullptr);
    MakeFlat(hBtnSettings);

    hBtnClose = CreateWindowExW(0, L"BUTTON", BTN_CLOSE,
        ctrlStyle, 0, 0, BTN_W, BTN_H,
        hwndParent, (HMENU)IDC_BTN_CLOSE, hInst, nullptr);
    MakeFlat(hBtnClose);

    // ---- 应用字体 ----
    // 标题栏：12pt bold
    if (hFontHeader) {
        SendMessageW(hHeader, WM_SETFONT, (WPARAM)hFontHeader, TRUE);
    }
    // ListView、按钮：9pt regular
    if (hFontNormal) {
        SendMessageW(hBtnPrevDay,   WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        SendMessageW(hBtnNextDay,   WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        SendMessageW(hBtnToday,     WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        SendMessageW(hListView,     WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        SendMessageW(hBtnRefresh,   WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        SendMessageW(hBtnIgnore,    WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        SendMessageW(hBtnSettings,  WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        SendMessageW(hBtnClose,     WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    }

    // ---- 初始化 DirectWrite（失败则继续使用 GDI 回退） ----
    InitDirectWrite();

    // ---- 子类化控件：替换 GDI 文本绘制为 DirectWrite ----
    // 头部 STATIC 控件
    g_pfnOrigHeaderProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hHeader, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(HeaderSubclassProc)));

    // ListView 控件
    g_pfnOrigListViewProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hListView, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(ListViewSubclassProc)));

    // ---- 子类化底部按钮：Win11 风格 D2D 绘制 ----
    // 仅对 4 个底部操作按钮，不包括（由 STATIC 绘制的）头部按钮
    HWND bottomBtns[] = { hBtnRefresh, hBtnIgnore, hBtnSettings, hBtnClose };
    for (HWND hBtn : bottomBtns) {
        if (hBtn && IsWindow(hBtn)) {
            g_btnOrigProc[hBtn] = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(hBtn, GWLP_WNDPROC,
                                  reinterpret_cast<LONG_PTR>(ButtonSubclassProc)));
            g_btnHover[hBtn]    = false;
            g_btnTracking[hBtn] = false;
        }
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
    std::wstring text;
    if (totalSeconds == 0) {
        text = wDate + L"  |  No data";
    } else {
        text = wDate + L"  |  " + FormatHeaderTotal(totalSeconds);
    }

    if (g_isHistoryMode) {
        text += L"  (history)";
    }

    // 缓存文本供 DirectWrite 子类化过程使用（仅 DWrite 绘制，不设 GDI 文本）
    g_sHeaderText = text;

    // 强制重绘并擦除背景（触发 HeaderSubclassProc 的 WM_PAINT）
    // TRUE = 擦除背景，清除上一帧可能的 GDI 残留
    InvalidateRect(hHeader, nullptr, TRUE);
}

// 头部总时长（纯 wstring，零栈缓冲区）
std::wstring FormatHeaderTotal(uint64_t totalSec) {
    uint64_t h = totalSec / 3600;
    uint64_t m = (totalSec % 3600) / 60;
    return L"Total: " + std::to_wstring(h) + L"h " + std::to_wstring(m) + L"m";
}

// ============================================================================
// InitDirectWrite — 初始化 Direct2D 工厂和 DirectWrite 文本格式
// 返回值：true 表示成功，false 表示失败（程序可继续运行，回退到 GDI）
// ============================================================================

bool InitDirectWrite() {
    HRESULT hr;

    // 创建 Direct2D 工厂（单线程模式，程序只在主 UI 线程访问）
    // D2D1_FACTORY_TYPE_SINGLE_THREADED: 无需跨线程同步，性能最优
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           __uuidof(ID2D1Factory),
                           reinterpret_cast<void**>(&g_pD2DFactory));
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PanelUI] D2D1CreateFactory failed\n");
        return false;
    }

    // 创建 DirectWrite 工厂（共享模式，允许系统缓存字体数据）
    // DWRITE_FACTORY_TYPE_SHARED: 多实例共享字体缓存，减少内存
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                              __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown**>(&g_pDWriteFactory));
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PanelUI] DWriteCreateFactory failed\n");
        SAFE_RELEASE_DW(g_pD2DFactory);
        return false;
    }

    // Header 文本格式：Segoe UI Bold 16 DIP (~12pt)
    hr = g_pDWriteFactory->CreateTextFormat(
        FONT_FACE, nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        HEADER_FONT_SIZE_DIP,
        L"en-us",
        &g_pTextFormatHeader);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PanelUI] CreateTextFormat(header) failed\n");
        CleanupDirectWrite();
        return false;
    }

    // 普通文本格式：Segoe UI Normal 12 DIP (~9pt)
    hr = g_pDWriteFactory->CreateTextFormat(
        FONT_FACE, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        NORMAL_FONT_SIZE_DIP,
        L"en-us",
        &g_pTextFormatNormal);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PanelUI] CreateTextFormat(normal) failed\n");
        CleanupDirectWrite();
        return false;
    }

    // Win11 列表项文本格式：Segoe UI Variable Normal 13.333 DIP (~10pt)
    hr = g_pDWriteFactory->CreateTextFormat(
        L"Segoe UI Variable", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        ITEM_FONT_SIZE_DIP,
        L"en-us",
        &g_pTextFormatItem);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[PanelUI] CreateTextFormat(item) failed\n");
        // 不致命 — 回退到 g_pTextFormatNormal
        g_pTextFormatItem = nullptr;
    }

    // 设置对齐方式
    g_pTextFormatHeader->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    g_pTextFormatHeader->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g_pTextFormatNormal->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    g_pTextFormatNormal->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (g_pTextFormatItem) {
        g_pTextFormatItem->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        g_pTextFormatItem->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    OutputDebugStringW(L"[PanelUI] DirectWrite initialized successfully\n");
    return true;
}

// ============================================================================
// CleanupDirectWrite — 释放所有 DirectWrite/Direct2D COM 对象
// 同时恢复子类化的窗口过程，避免崩溃
// ============================================================================

void CleanupDirectWrite() {
    // 恢复子类化窗口过程
    if (g_pfnOrigHeaderProc && hHeader && IsWindow(hHeader)) {
        SetWindowLongPtrW(hHeader, GWLP_WNDPROC, (LONG_PTR)g_pfnOrigHeaderProc);
    }
    if (g_pfnOrigListViewProc && hListView && IsWindow(hListView)) {
        SetWindowLongPtrW(hListView, GWLP_WNDPROC, (LONG_PTR)g_pfnOrigListViewProc);
    }
    g_pfnOrigHeaderProc = nullptr;
    g_pfnOrigListViewProc = nullptr;

    // 恢复子类化的按钮窗口过程
    for (auto& pair : g_btnOrigProc) {
        if (IsWindow(pair.first)) {
            SetWindowLongPtrW(pair.first, GWLP_WNDPROC, (LONG_PTR)pair.second);
        }
    }
    g_btnOrigProc.clear();
    g_btnHover.clear();
    g_btnTracking.clear();

    // 释放 DirectWrite 文本格式
    SAFE_RELEASE_DW(g_pTextFormatItem);
    SAFE_RELEASE_DW(g_pTextFormatNormal);
    SAFE_RELEASE_DW(g_pTextFormatHeader);
    // 释放 DWrite 工厂
    SAFE_RELEASE_DW(g_pDWriteFactory);
    // 释放 D2D 工厂（最后释放，因为其他对象依赖于它——引用计数管理）
    SAFE_RELEASE_DW(g_pD2DFactory);

    g_listItemDisplay.clear();
    g_sHeaderText.clear();
}

// ============================================================================
// HeaderSubclassProc — 头部 STATIC 控件的子类化窗口过程
// 使用 DirectWrite 绘制日期/总时长文本，实现超清晰字体渲染
// 回退策略：如果 D2D 不可用，调用原始窗口过程回退到 GDI
// ============================================================================

static LRESULT CALLBACK HeaderSubclassProc(HWND hwnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        // 如果 DWrite 未初始化或无可绘制文本，回退到 GDI
        if (!g_pD2DFactory || !g_pTextFormatHeader || g_sHeaderText.empty()) {
            return CallWindowProcW(g_pfnOrigHeaderProc, hwnd, msg, wParam, lParam);
        }

        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (!hdc) return 0;

        RECT rc;
        GetClientRect(hwnd, &rc);

        // 创建 DC 渲染目标（绑定到 BeginPaint 提供的 HDC）
        D2D1_RENDER_TARGET_PROPERTIES props = {};
        props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;

        ID2D1DCRenderTarget* pRT = nullptr;
        HRESULT hr = g_pD2DFactory->CreateDCRenderTarget(&props, &pRT);
        if (SUCCEEDED(hr)) {
            hr = pRT->BindDC(hdc, &rc);
            if (SUCCEEDED(hr)) {
                // DPI 感知（适配系统缩放比例）
                FLOAT dpiX, dpiY;
                pRT->GetDpi(&dpiX, &dpiY);

                pRT->BeginDraw();
                pRT->SetDpi(dpiX, dpiY);

                // 使用与父窗口背景一致的浅灰色填充（RGB(243,243,243)=0xF3F3F3）
                // 避免上一帧残留的 GDI 文本透过 DWrite 文字层显示
                pRT->Clear(D2D1::ColorF(0xF3F3F3));

                // ---- Win11 风格卡片背景：白色圆角矩形，80% 不透明度，4px 内边距 ----
                const float CARD_RADIUS = 8.0f;
                const float CARD_PAD    = 4.0f;
                FLOAT cardW = static_cast<FLOAT>(rc.right - rc.left) - 2 * CARD_PAD;
                FLOAT cardH = static_cast<FLOAT>(rc.bottom - rc.top) - 2 * CARD_PAD;
                if (cardW > CARD_RADIUS * 2 && cardH > CARD_RADIUS * 2) {
                    D2D1_ROUNDED_RECT cardRR = D2D1::RoundedRect(
                        D2D1::RectF(CARD_PAD, CARD_PAD,
                                    CARD_PAD + cardW, CARD_PAD + cardH),
                        CARD_RADIUS, CARD_RADIUS);
                    ID2D1RoundedRectangleGeometry* pCardGeom = nullptr;
                    HRESULT hrGeom = g_pD2DFactory->CreateRoundedRectangleGeometry(
                        cardRR, &pCardGeom);
                    if (SUCCEEDED(hrGeom) && pCardGeom) {
                        ID2D1SolidColorBrush* pCardBrush = nullptr;
                        HRESULT hrBr = pRT->CreateSolidColorBrush(
                            D2D1::ColorF(0xFFFFFF, 0.8f), &pCardBrush);
                        if (SUCCEEDED(hrBr) && pCardBrush) {
                            pRT->FillGeometry(pCardGeom, pCardBrush);
                            pCardBrush->Release();
                        }
                        pCardGeom->Release();
                    }
                }

                // 创建文字笔刷（深灰 #1A1A1A = RGB(26,26,26)）
                ID2D1SolidColorBrush* pBrush = nullptr;
                hr = pRT->CreateSolidColorBrush(
                    D2D1::ColorF(0x1A1A1A), &pBrush);
                if (SUCCEEDED(hr) && pBrush) {
                    // 文本区域：卡片内部，留 4px 内边距
                    D2D1_RECT_F textRect = {
                        CARD_PAD + 2.0f, CARD_PAD,
                        static_cast<FLOAT>(rc.right - rc.left) - CARD_PAD,
                        static_cast<FLOAT>(rc.bottom - rc.top) - CARD_PAD
                    };

                    // 使用 DirectWrite 绘制文本（ClearType 抗锯齿）
                    pRT->DrawTextW(
                        g_sHeaderText.c_str(),
                        (UINT32)g_sHeaderText.size(),
                        g_pTextFormatHeader,
                        textRect,
                        pBrush,
                        D2D1_DRAW_TEXT_OPTIONS_CLIP |
                        D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);

                    pBrush->Release();
                }

                // 提交 D2D 绘制到 HDC
                hr = pRT->EndDraw();
                if (FAILED(hr)) {
                    OutputDebugStringW(L"[PanelUI] Header D2D EndDraw failed, "
                                       L"returning empty\n");
                    // EndPaint 已调用，不能再次触发原始 WM_PAINT（会导致双重绘制）
                    // 返回 0，区域保持 Clear() 后的空背景色
                    pRT->Release();
                    EndPaint(hwnd, &ps);
                    return 0;
                }
            }
            pRT->Release();
        } else {
            // D2D 创建失败，区域保持父窗口背景色
            EndPaint(hwnd, &ps);
            return 0;
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        // 完全由 WM_PAINT 处理绘制，禁止擦除（避免闪烁）
        return 1;

    case WM_SIZE:
        // 窗口大小改变 → 强制重绘并擦除背景（适配新尺寸的 D2D 渲染目标）
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    default:
        break;
    }

    return CallWindowProcW(g_pfnOrigHeaderProc, hwnd, msg, wParam, lParam);
}

// ============================================================================
// ListViewSubclassProc — ListView 控件的子类化窗口过程
// 使用 DirectWrite 绘制所有列表项文本（列标题由 Header 子控件自行处理）
// 支持：选择高亮、图标、网格线、文本列对齐
// 回退策略：D2D 不可用 → 调用原始窗口过程
// ============================================================================

static LRESULT CALLBACK ListViewSubclassProc(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        // 如果 DWrite 未初始化，回退到 GDI
        if (!g_pD2DFactory || !g_pTextFormatNormal) {
            return CallWindowProcW(g_pfnOrigListViewProc, hwnd, msg, wParam, lParam);
        }

        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (!hdc) return 0;

        RECT rc;
        GetClientRect(hwnd, &rc);

        // 创建 DC 渲染目标
        D2D1_RENDER_TARGET_PROPERTIES props = {};
        props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;

        ID2D1DCRenderTarget* pRT = nullptr;
        HRESULT hr = g_pD2DFactory->CreateDCRenderTarget(&props, &pRT);
        if (FAILED(hr)) {
            // D2D 不可用 → 返回 0（区域保持白色背景）
            EndPaint(hwnd, &ps);
            return 0;
        }

        hr = pRT->BindDC(hdc, &rc);
        if (FAILED(hr)) {
            pRT->Release();
            // BindDC 失败 → 返回 0，区域保持白色背景
            EndPaint(hwnd, &ps);
            return 0;
        }

        pRT->BeginDraw();

        // ---- 清空背景（白色） ----
        pRT->Clear(D2D1::ColorF(D2D1::ColorF::White));

        // 获取列表状态
        int itemCount = ListView_GetItemCount(hwnd);
        int topIdx = ListView_GetTopIndex(hwnd);
        int countPerPage = ListView_GetCountPerPage(hwnd);
        int bottomIdx = min(topIdx + countPerPage, itemCount);

        if (topIdx < 0 || itemCount <= 0 || itemCount != (int)g_listItemDisplay.size()) {
            // 无数据或数据不一致 → 只画背景即可
            goto EndListViewDraw;
        }

        // ---- 循环绘制所有可见行的 Win11 风格条目 ----
        int selIdx = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);

        for (int idx = topIdx; idx < bottomIdx && idx < (int)g_listItemDisplay.size(); ++idx) {
            RECT rcItem = {};
            if (SendMessageW(hwnd, LVM_GETITEMRECT, (WPARAM)idx, (LPARAM)&rcItem)) {
                bool isSel = (idx == selIdx);

                // 整行宽度（使用 ListView 客户区宽度）
                RECT rcFull = rcItem;
                rcFull.left = 0;
                rcFull.right = rc.right - rc.left;

                // 绘制 Win11 风格行
                DrawListViewItem_Win11(pRT, rcFull,
                                       g_listItemDisplay[idx], isSel);
            }
        }

        // 提交 D2D 绘制
        hr = pRT->EndDraw();
        if (FAILED(hr)) {
            OutputDebugStringW(L"[PanelUI] ListView D2D EndDraw failed\n");
            pRT->Release();
            EndPaint(hwnd, &ps);
            return 0;
        }
        pRT->Release();

        // ---- D2D 绘制完成后，使用 GDI 循环绘制所有可见行的图标 ----
        HIMAGELIST himl = AppIconCache::GetImageList();
        if (himl) {
            for (int idx = topIdx; idx < bottomIdx && idx < (int)g_listItemDisplay.size(); ++idx) {
                int iconIdx = g_listItemDisplay[idx].iconIndex;
                if (iconIdx >= 0) {
                    RECT rcIconItem = {};
                    if (SendMessageW(hwnd, LVM_GETITEMRECT,
                                     (WPARAM)idx, (LPARAM)&rcIconItem)) {
                        int iconItemH = rcIconItem.bottom - rcIconItem.top;
                        int iconY = rcIconItem.top + (iconItemH - ICON_SIZE) / 2;
                        ImageList_Draw(himl, iconIdx, hdc,
                                       12, iconY, ILD_TRANSPARENT);
                    }
                }
            }
        }

    EndListViewDraw:
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        // 完全由 WM_PAINT 绘制，禁止擦除（避免闪烁）
        return 1;

    case WM_SIZE:
        // 窗口大小改变 → 强制重绘并擦除背景
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    default:
        break;
    }

    return CallWindowProcW(g_pfnOrigListViewProc, hwnd, msg, wParam, lParam);
}

// ============================================================================
// ButtonSubclassProc — 底部按钮的子类化窗口过程
// 使用 Direct2D 绘制 Win11 风格圆角按钮（替代 GDI BS_FLAT）
// 回退策略：D2D 不可用 → 调用原始窗口过程（原始 GDI 按钮外观）
// 点击/焦点/键盘行为 → 全部转发给原始窗口过程
// ============================================================================

// ============================================================================
// DrawButtonD2D_Content — D2D 绘制按钮内容（共享：WM_PAINT / WM_PRINTCLIENT）
// pRT:  已 BeginDraw 的渲染目标
// hwnd: 按钮窗口句柄（用于读取文本、状态）
// rc:   按钮客户区矩形
// ============================================================================
static void DrawButtonD2D_Content(ID2D1RenderTarget* pRT, HWND hwnd, const RECT& rc) {
    // 清空背景（父窗口背景色 #F3F3F3，填充按钮圆角之外的区域）
    pRT->Clear(D2D1::ColorF(0xF3F3F3));

    FLOAT x = (FLOAT)rc.left;
    FLOAT y = (FLOAT)rc.top;
    FLOAT w = (FLOAT)(rc.right - rc.left);
    FLOAT h = (FLOAT)(rc.bottom - rc.top);

    // ---- 确定按钮状态（normal / hover / pressed） ----
    bool isHover   = g_btnHover.count(hwnd) ? g_btnHover[hwnd] : false;
    bool isPressed = (SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;

    D2D1_COLOR_F fillCol, borderCol;
    if (isPressed) {
        fillCol   = D2D1::ColorF(0xCCCCCC);
        borderCol = D2D1::ColorF(0xB0B0B0);
    } else if (isHover) {
        fillCol   = D2D1::ColorF(0xE0E0E0);
        borderCol = D2D1::ColorF(0xB0B0B0);
    } else {
        fillCol   = D2D1::ColorF(0xF5F5F5);
        borderCol = D2D1::ColorF(0xD0D0D0);
    }

    const float BTN_RADIUS = 4.0f;

    // ---- 圆角矩形背景 + 1px 边框 ----
    D2D1_ROUNDED_RECT btnRR = D2D1::RoundedRect(
        D2D1::RectF(x + 1.0f, y + 1.0f, x + w - 1.0f, y + h - 1.0f),
        BTN_RADIUS, BTN_RADIUS);

    ID2D1RoundedRectangleGeometry* pGeom = nullptr;
    HRESULT hr = g_pD2DFactory->CreateRoundedRectangleGeometry(btnRR, &pGeom);
    if (SUCCEEDED(hr) && pGeom) {
        // 填充
        ID2D1SolidColorBrush* pFillBr = nullptr;
        hr = pRT->CreateSolidColorBrush(fillCol, &pFillBr);
        if (SUCCEEDED(hr) && pFillBr) {
            pRT->FillGeometry(pGeom, pFillBr);
            pFillBr->Release();
        }
        // 1px 描边边框
        ID2D1SolidColorBrush* pBorderBr = nullptr;
        hr = pRT->CreateSolidColorBrush(borderCol, &pBorderBr);
        if (SUCCEEDED(hr) && pBorderBr) {
            pRT->DrawGeometry(pGeom, pBorderBr, 1.0f);
            pBorderBr->Release();
        }
        pGeom->Release();
    }

    // ---- 按钮文本（水平居中，垂直居中） ----
    int textLen = GetWindowTextLengthW(hwnd);
    if (textLen > 0) {
        std::wstring text(textLen, L'\0');
        GetWindowTextW(hwnd, &text[0], textLen + 1);

        // 临时切换文本对齐为居中（同一线程同步绘制，安全）
        g_pTextFormatNormal->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        g_pTextFormatNormal->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        ID2D1SolidColorBrush* pTextBr = nullptr;
        hr = pRT->CreateSolidColorBrush(D2D1::ColorF(0x1A1A1A), &pTextBr);
        if (SUCCEEDED(hr) && pTextBr) {
            // 按下状态：文本偏移 +1px 模拟 Win11 "推入"效果
            FLOAT offX = isPressed ? 1.0f : 0.0f;
            FLOAT offY = isPressed ? 1.0f : 0.0f;
            D2D1_RECT_F textRect = {
                x + 4.0f + offX, y + 2.0f + offY,
                x + w - 4.0f + offX, y + h - 2.0f + offY
            };
            pRT->DrawTextW(
                text.c_str(), (UINT32)text.size(),
                g_pTextFormatNormal, textRect,
                pTextBr,
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
            pTextBr->Release();
        }

        // 恢复原始对齐
        g_pTextFormatNormal->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        g_pTextFormatNormal->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

// ============================================================================
// ButtonSubclassProc — 底部按钮的子类化窗口过程
// 使用 Direct2D 绘制 Win11 风格圆角按钮（替代 GDI BS_FLAT）
// 回退策略：D2D 不可用 → 调用原始窗口过程（原始 GDI 按钮外观）
// 点击/焦点/键盘行为 → 全部转发给原始窗口过程
// ============================================================================

static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        // D2D 未就绪 → 回退到原始 GDI 按钮
        if (!g_pD2DFactory || !g_pTextFormatNormal) break;

        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (!hdc) return 0;

        RECT rc;
        GetClientRect(hwnd, &rc);

        D2D1_RENDER_TARGET_PROPERTIES props = {};
        props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;

        ID2D1DCRenderTarget* pRT = nullptr;
        HRESULT hr = g_pD2DFactory->CreateDCRenderTarget(&props, &pRT);
        if (FAILED(hr)) { EndPaint(hwnd, &ps); return 0; }

        hr = pRT->BindDC(hdc, &rc);
        if (FAILED(hr)) { pRT->Release(); EndPaint(hwnd, &ps); return 0; }

        pRT->BeginDraw();
        DrawButtonD2D_Content(pRT, hwnd, rc);

        hr = pRT->EndDraw();
        if (FAILED(hr)) {
            OutputDebugStringW(L"[PanelUI] Button D2D EndDraw failed\n");
            pRT->Release();
            EndPaint(hwnd, &ps);
            return 0;
        }
        pRT->Release();

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_PRINTCLIENT: {
        // WM_PRINTCLIENT 由按钮原始过程在状态变更时发送，
        // 若不拦截会通过 CallWindowProcW 触发 GDI 绘制 → 导致 GDI 文本闪烁
        if (!g_pD2DFactory || !g_pTextFormatNormal) break;
        HDC hdc = (HDC)wParam;
        if (!hdc) return 0;

        RECT rc;
        GetClientRect(hwnd, &rc);

        D2D1_RENDER_TARGET_PROPERTIES props = {};
        props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;

        ID2D1DCRenderTarget* pRT = nullptr;
        HRESULT hr = g_pD2DFactory->CreateDCRenderTarget(&props, &pRT);
        if (FAILED(hr)) return 0;

        hr = pRT->BindDC(hdc, &rc);
        if (FAILED(hr)) { pRT->Release(); return 0; }

        pRT->BeginDraw();
        DrawButtonD2D_Content(pRT, hwnd, rc);
        pRT->EndDraw();
        pRT->Release();
        return 0;
    }

    case WM_MOUSEMOVE: {
        bool wasHover = g_btnHover.count(hwnd) ? g_btnHover[hwnd] : false;
        g_btnHover[hwnd] = true;
        // 首次 hover → 请求 WM_MOUSELEAVE 通知
        if (!g_btnTracking.count(hwnd) || !g_btnTracking[hwnd]) {
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_btnTracking[hwnd] = true;
        }
        if (!wasHover) InvalidateRect(hwnd, nullptr, FALSE);
        break; // 同时传递给原始窗口过程（BS_FLAT 对此无特殊行为）
    }

    case WM_MOUSELEAVE: {
        g_btnHover[hwnd]    = false;
        g_btnTracking[hwnd] = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0; // 已处理，不传递给原始窗口过程
    }

    case WM_ERASEBKGND:
        // 完全由 WM_PAINT 处理绘制，禁止擦除（避免闪烁）
        return 1;

    case WM_DESTROY: {
        // 清理该按钮的状态映射
        g_btnOrigProc.erase(hwnd);
        g_btnHover.erase(hwnd);
        g_btnTracking.erase(hwnd);
        break;
    }

    default:
        // 安全守卫：绝不将 WM_PAINT 转发给原始按钮过程（避免 GDI 双重绘制）
        if (msg == WM_PAINT) return 0;
        break;
    }

    // 转发给原始窗口过程（保留按钮的点击/焦点/键盘/通知行为）
    auto it = g_btnOrigProc.find(hwnd);
    if (it != g_btnOrigProc.end()) {
        return CallWindowProcW(it->second, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// DrawListViewItem_Win11 — 使用 Direct2D 绘制单个 Win11 File Explorer 风格行（原型）
// pRT:       处于 BeginDraw 状态的 D2D 渲染目标
// rc:        该行的客户区矩形（LEFT/TOP 为行左/顶部，整个行宽）
// item:      该行的数据（displayName, duration, percent, status, iconIndex）
// isSelected: 选中状态（影响背景色 #E8F0FE）
// 注意：应用图标由调用方在 EndDraw 之后通过 GDI ImageList_Draw 绘制
// ============================================================================

void DrawListViewItem_Win11(ID2D1RenderTarget* pRT, const RECT& rc,
                            const ListItemDisplay& item, bool isSelected) {
    // ---- 布局常量 ----
    const float RADIUS        = 4.0f;   // 圆角半径
    const int   LEFT_MARGIN   = 16;     // 左边距
    const int   ICON_SIZE_PX  = 20;     // 图标占位宽度
    const int   ICON_GAP      = 8;      // 图标与文字间距
    const int   DUR_WIDTH     = 80;     // 时长列宽
    const int   PCT_WIDTH     = 55;     // 百分比列宽
    const int   STATUS_WIDTH  = 80;     // 状态标识区域宽度
    const float DOT_RADIUS    = 5.0f;   // 绿点半径 (10px 直径)

    FLOAT x = (FLOAT)rc.left;
    FLOAT y = (FLOAT)rc.top;
    FLOAT w = (FLOAT)(rc.right - rc.left);
    FLOAT h = (FLOAT)(rc.bottom - rc.top);

    // 10pt 文本格式回退：如果 g_pTextFormatItem 未创建则使用 g_pTextFormatNormal
    IDWriteTextFormat* pFmtItem = g_pTextFormatItem ? g_pTextFormatItem
                                                    : g_pTextFormatNormal;

    // ---- 1. 圆角矩形背景（仅选中时） ----
    if (isSelected) {
        ID2D1RoundedRectangleGeometry* pGeom = nullptr;
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            D2D1::RectF(x, y + 1.0f, x + w, y + h - 1.0f), RADIUS, RADIUS);
        HRESULT hrGeom = g_pD2DFactory->CreateRoundedRectangleGeometry(rr, &pGeom);
        if (SUCCEEDED(hrGeom) && pGeom) {
            ID2D1SolidColorBrush* pBgBrush = nullptr;
            HRESULT hrBr = pRT->CreateSolidColorBrush(
                D2D1::ColorF(0xE8F0FE), &pBgBrush);
            if (SUCCEEDED(hrBr) && pBgBrush) {
                pRT->FillGeometry(pGeom, pBgBrush);
                pBgBrush->Release();
            }
            pGeom->Release();
        }
    }

    // ---- 2. 应用名称（图标右侧，左对齐） ----
    FLOAT textX = x + LEFT_MARGIN + ICON_SIZE_PX + ICON_GAP; // 16+20+8 = 44

    // 计算右侧元素总占位宽度
    FLOAT rightAreaW = DUR_WIDTH + PCT_WIDTH + STATUS_WIDTH;
    FLOAT textMaxW = w - textX - rightAreaW - LEFT_MARGIN;
    if (textMaxW < 60.0f) textMaxW = 60.0f;

    // 文字笔刷（#1A1A1A 深灰）
    ID2D1SolidColorBrush* pNameBrush = nullptr;
    HRESULT hrNameBr = pRT->CreateSolidColorBrush(D2D1::ColorF(0x1A1A1A), &pNameBrush);

    D2D1_RECT_F nameRect = { textX, y + 1.0f, textX + textMaxW, y + h - 1.0f };
    if (SUCCEEDED(hrNameBr) && pNameBrush && !item.displayName.empty()) {
        pRT->DrawTextW(
            item.displayName.c_str(),
            (UINT32)item.displayName.size(),
            pFmtItem,
            nameRect,
            pNameBrush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    if (pNameBrush) pNameBrush->Release();

    // ---- 3. 时长（右侧，颜色 #555555） ----
    FLOAT durX = x + w - STATUS_WIDTH - PCT_WIDTH - DUR_WIDTH - LEFT_MARGIN;

    ID2D1SolidColorBrush* pDurBrush = nullptr;
    HRESULT hrDurBr = pRT->CreateSolidColorBrush(D2D1::ColorF(0x555555), &pDurBrush);

    D2D1_RECT_F durRect = { durX, y + 1.0f, durX + DUR_WIDTH - 4, y + h - 1.0f };
    if (SUCCEEDED(hrDurBr) && pDurBrush && !item.duration.empty()) {
        pRT->DrawTextW(
            item.duration.c_str(),
            (UINT32)item.duration.size(),
            pFmtItem,
            durRect,
            pDurBrush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    if (pDurBrush) pDurBrush->Release();

    // ---- 4. 百分比（时长右侧，颜色 #888888） ----
    FLOAT pctX = durX + DUR_WIDTH;
    ID2D1SolidColorBrush* pPctBrush = nullptr;
    HRESULT hrPctBr = pRT->CreateSolidColorBrush(D2D1::ColorF(0x888888), &pPctBrush);

    D2D1_RECT_F pctRect = { pctX, y + 1.0f, pctX + PCT_WIDTH - 4, y + h - 1.0f };
    if (SUCCEEDED(hrPctBr) && pPctBrush && !item.percent.empty()) {
        pRT->DrawTextW(
            item.percent.c_str(),
            (UINT32)item.percent.size(),
            pFmtItem,
            pctRect,
            pPctBrush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    if (pPctBrush) pPctBrush->Release();

    // ---- 5. 状态徽标（绿点 + "Active" 文字，仅当 status 包含 "ACTIVE"） ----
    const bool hasDot =
        (item.status.find(L"ACTIVE") != std::wstring::npos ||
         item.status.find(L"Active") != std::wstring::npos);

    if (hasDot) {
        FLOAT badgeX = pctX + PCT_WIDTH + 8;
        FLOAT dotCenterX = badgeX + DOT_RADIUS;
        FLOAT dotCenterY = y + h / 2.0f;

        // 绿色小圆点（#4CAF50）
        ID2D1SolidColorBrush* pDotBrush = nullptr;
        HRESULT hrDotBr = pRT->CreateSolidColorBrush(D2D1::ColorF(0x4CAF50), &pDotBrush);
        if (SUCCEEDED(hrDotBr) && pDotBrush) {
            D2D1_ELLIPSE dot = { { dotCenterX, dotCenterY }, DOT_RADIUS, DOT_RADIUS };
            pRT->FillEllipse(&dot, pDotBrush);
            pDotBrush->Release();
        }

        // "Active" 文字（#4CAF50 同色）
        FLOAT activeTextX = dotCenterX + DOT_RADIUS + 6;
        ID2D1SolidColorBrush* pActiveBrush = nullptr;
        HRESULT hrActBr = pRT->CreateSolidColorBrush(D2D1::ColorF(0x4CAF50), &pActiveBrush);
        if (SUCCEEDED(hrActBr) && pActiveBrush) {
            std::wstring activeStr = L"Active";
            D2D1_RECT_F activeRect = {
                activeTextX, y + 1.0f,
                activeTextX + STATUS_WIDTH - (activeTextX - badgeX) - 4,
                y + h - 1.0f
            };
            pRT->DrawTextW(
                activeStr.c_str(), (UINT32)activeStr.size(),
                pFmtItem,
                activeRect,
                pActiveBrush,
                D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            pActiveBrush->Release();
        }
    }
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
        if (g_lastActiveIndex < (int)g_listItemDisplay.size()) {
            g_listItemDisplay[g_lastActiveIndex].status.clear();
        }
    }

    // 遍历并更新每行的秒数数据（所有条目都可能更新）
    for (size_t i = 0; i < g_displayedAppPaths.size(); ++i) {
        // 查找该路径在 today.entries 中的最新秒数
        for (const auto& entry : today.entries) {
            if (_wcsicmp(entry.appPath.c_str(), g_displayedAppPaths[i].c_str()) == 0) {
                std::wstring dur = FormatDurationW(entry.seconds);
                std::wstring pct = FormatPercentW(entry.seconds, today.totalSeconds);

                // 更新 GDI 文本（辅助功能需要）
                LvSetItemText(hListView, (int)i, 1, dur.c_str());
                LvSetItemText(hListView, (int)i, 2, pct.c_str());

                // 更新 DirectWrite 缓存数据
                if (i < g_listItemDisplay.size()) {
                    g_listItemDisplay[i].duration = dur;
                    g_listItemDisplay[i].percent = pct;
                }

                // 标记是否为当前活动应用
                bool isCurrent = (_wcsicmp(g_displayedAppPaths[i].c_str(), currentApp.c_str()) == 0);
                if (isCurrent) {
                    currentIdx = (int)i;
                }
                break;
            }
        }
    }

    // 设置新 [ACTIVE] 状态
    if (currentIdx >= 0) {
        LvSetItemText(hListView, currentIdx, 3, L"[ACTIVE]");
        if (currentIdx < (int)g_listItemDisplay.size()) {
            g_listItemDisplay[currentIdx].status = L"[ACTIVE]";
        }
        g_lastActiveIndex = currentIdx;
    } else {
        g_lastActiveIndex = -1;
    }

    // 更新 appToIndex 映射
    g_appToIndex.clear();
    for (size_t i = 0; i < g_displayedAppPaths.size(); ++i) {
        g_appToIndex[g_displayedAppPaths[i]] = (int)i;
    }

    // 触发 DirectWrite 重绘（每秒更新）
    InvalidateRect(hListView, nullptr, FALSE);
}

// ============================================================================
// RefreshFull — 全量重建 ListView
// ============================================================================

void RefreshFull(const DailyData& data) {
    // 清除旧数据
    ListView_DeleteAllItems(hListView);
    g_appToIndex.clear();
    g_displayedAppPaths.clear();
    g_listItemDisplay.clear();
    g_lastActiveIndex = -1;

    // 更新头部
    UpdateHeader(data.date, data.totalSeconds);

    if (data.entries.empty()) {
        InvalidateRect(hListView, nullptr, FALSE);
        return;
    }

    // 按秒数降序排列
    std::vector<AppTimeEntry> sorted = data.entries;
    std::sort(sorted.begin(), sorted.end(),
        [](const AppTimeEntry& a, const AppTimeEntry& b) {
            return a.seconds > b.seconds;
        });

    // 添加到 ListView（同时填充 g_listItemDisplay 供 DirectWrite 绘制）
    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& entry = sorted[i];

        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_IMAGE;
        item.iItem = (int)i;
        item.pszText = const_cast<LPWSTR>(entry.displayName.c_str());
        int iconIdx = AppIconCache::GetOrLoadIcon(entry.appPath);
        item.iImage = iconIdx;

        // 如果显示名称为空，使用文件基本名
        std::wstring fallbackName;
        if (entry.displayName.empty()) {
            size_t pos = entry.appPath.find_last_of(L"\\/");
            fallbackName = (pos != std::wstring::npos)
                ? entry.appPath.substr(pos + 1) : entry.appPath;
            item.pszText = const_cast<LPWSTR>(fallbackName.c_str());
        }

        // 构建 DirectWrite 显示数据（在 GDI 插入之前）
        ListItemDisplay ld;
        ld.displayName = entry.displayName.empty() ? fallbackName : entry.displayName;
        ld.duration = FormatDurationW(entry.seconds);
        ld.percent = FormatPercentW(entry.seconds, data.totalSeconds);
        ld.status = L"";
        ld.iconIndex = iconIdx;
        g_listItemDisplay.push_back(ld);

        int idx = ListView_InsertItem(hListView, &item);
        if (idx >= 0) {
            // 设置 GDI 文本（辅助功能/无障碍使用需要）
            LvSetItemText(hListView, idx, 1, ld.duration.c_str());
            LvSetItemText(hListView, idx, 2, ld.percent.c_str());
            LvSetItemText(hListView, idx, 3, ld.status.c_str());
        }

        g_appToIndex[entry.appPath] = idx;
        g_displayedAppPaths.push_back(entry.appPath);
    }

    // 恢复历史模式标记
    if (g_isHistoryMode) {
        UpdateHeader(data.date, data.totalSeconds);
    }

    // 触发 DirectWrite 重绘
    InvalidateRect(hListView, nullptr, FALSE);
}

// ============================================================================
// LoadHistoryData — 加载历史日期数据
// ============================================================================

void LoadHistoryData(const std::string& date, const DailyData& data) {
    ListView_DeleteAllItems(hListView);
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
    // 清理 DirectWrite/Direct2D 资源（释放 COM 对象，恢复子类化过程）
    CleanupDirectWrite();

    // 销毁图标 ImageList（释放所有托管图标）
    AppIconCache::Cleanup();

    // 所有句柄由父窗口 DestroyWindow 自动销毁，此处无需额外释放
    // 仅清除映射数据
    g_appToIndex.clear();
    g_displayedAppPaths.clear();
    g_listItemDisplay.clear();
    g_lastActiveIndex = -1;
    g_sHeaderText.clear();
}

} // namespace PanelUI
