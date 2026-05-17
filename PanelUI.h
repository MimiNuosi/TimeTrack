// TimeTrack - 统计面板 UI 管理
// 职责：主窗口中所有子控件的创建、布局缩放、数据刷新
// 包括：头部日期/时长标签、日期导航按钮、ListView、底部按钮栏

#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <map>
#include "Types.h"

// Direct2D / DirectWrite 前置声明（避免在头文件中引入完整 d2d1.h/dwrite.h）
struct ID2D1Factory;
struct IDWriteFactory;
struct IDWriteTextFormat;

namespace PanelUI {

// ---- 控件句柄（供外部查询和消息处理） ----
extern HWND hHeader;       // IDC_HEADER    - 日期 + 总时长静态文本
extern HWND hBtnPrevDay;   // IDC_BTN_PREV_DAY   - ◀ 前一天
extern HWND hBtnNextDay;   // IDC_BTN_NEXT_DAY   - ▶ 后一天
extern HWND hBtnToday;     // IDC_BTN_TODAY      - [Today]
extern HWND hListView;     // IDC_LISTVIEW       - 主 ListView
extern HWND hBtnRefresh;   // IDC_BTN_REFRESH
extern HWND hBtnIgnore;    // IDC_BTN_IGNORE
extern HWND hBtnSettings;  // IDC_BTN_SETTINGS
extern HWND hBtnClose;     // IDC_BTN_CLOSE

// ---- 状态 ----
extern bool        g_isHistoryMode;       // 当前是否在查看历史数据
extern std::string g_currentViewDate;     // 当前显示的日期 "YYYY-MM-DD"
extern int         g_lastActiveIndex;     // 上一次标记为 [ACTIVE] 的行（用于清除）

// ---- 索引映射：应用路径 → ListView 行号（用于增量刷新时快速定位） ----
extern std::map<std::wstring, int> g_appToIndex;
// 当前显示在 ListView 中的条目路径列表（与行号一一对应）
extern std::vector<std::wstring> g_displayedAppPaths;

// ---- 公开接口 ----

// 在 WM_CREATE 时调用：创建所有子控件
// hwndParent:   主窗口句柄
// hInst:        HINSTANCE 用于 CreateWindowEx
// hFontHeader:  12pt bold 字体（标题栏用）
// hFontNormal:  9pt regular 字体（ListView / 按钮用）
void Initialize(HWND hwndParent, HINSTANCE hInst, HFONT hFontHeader, HFONT hFontNormal);

// 在 WM_SIZE 时调用：重新布局所有控件
// cx, cy: 窗口客户区宽度和高度
void Resize(int cx, int cy);

// 增量刷新：仅更新头部总时长 + 当前计时应用行（每秒调用）
// today:      今日完整数据
// currentApp: 当前正在计时的应用路径
void RefreshIncremental(const DailyData& today, const std::wstring& currentApp,
                         const std::wstring& displayName);

// 全量刷新：重建整个 ListView（日期切换或应用列表变化时调用）
// data: 要显示的完整日数据
void RefreshFull(const DailyData& data);

// 加载历史日期数据到 ListView
// date: 所选日期
// data: 该日的 DailyData
void LoadHistoryData(const std::string& date, const DailyData& data);

// 切换历史/实时模式
void SetHistoryMode(bool isHistory);

// 处理 ListView 右键菜单（NM_RCLICK）
// 返回被点击行的应用路径（空字符串表示未命中任何行）
std::wstring GetClickedAppPath(HWND hwnd, LPARAM lParam);

// 获取 ListView 当前选中行的应用路径
std::wstring GetSelectedAppPath();

// 更新头部日期和总时长文本
void UpdateHeader(const std::string& date, uint64_t totalSeconds);

// 格式化总秒数为显示文本 "Total: 2h 35m"
std::wstring FormatHeaderTotal(uint64_t totalSec);

// 清理资源（WM_DESTROY 时调用）
void Cleanup();

// ---- DirectWrite 文字渲染（替代 GDI，超清晰字体） ----
extern ID2D1Factory*       g_pD2DFactory;
extern IDWriteFactory*     g_pDWriteFactory;
extern IDWriteTextFormat*  g_pTextFormatHeader;  // Segoe UI 12pt Bold
extern IDWriteTextFormat*  g_pTextFormatNormal;  // Segoe UI 9pt Normal

// 缓存的列表条目数据，供 DirectWrite 自定义绘制使用
struct ListItemDisplay {
    std::wstring displayName;
    std::wstring duration;
    std::wstring percent;
    std::wstring status;
    int iconIndex = -1;
};
extern std::vector<ListItemDisplay> g_listItemDisplay;

// 初始化 DirectWrite（在 Initialize 中调用），返回 true 表示成功
bool InitDirectWrite();

// 清理 DirectWrite 资源，恢复子类化窗口过程
void CleanupDirectWrite();

} // namespace PanelUI
