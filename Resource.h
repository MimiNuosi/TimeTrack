// TimeTrack - 资源 ID 定义
// 图标、菜单命令、自定义消息、控件 ID

#pragma once

// ---- 图标 ----
#define IDI_TIMETRACK         101

// ---- 自定义消息 ----
#define WM_APP_TRAYICON       (WM_APP + 100)

// ---- 托盘菜单命令 ID ----
#define IDM_OPEN_PANEL        1001
#define IDM_TOGGLE_PAUSE      1002
#define IDM_SETTINGS          1003
#define IDM_EXIT              1004

// ---- 定时器 ID ----
#define TIMER_POLLING         1       // 窗口轮询定时器（计时逻辑）
#define TIMER_SAVE            2       // 定期保存定时器（备用）
#define TIMER_REFRESH_UI      3       // ListView 刷新定时器（1秒）

// ============================================================================
// Phase 4: 统计面板控件 ID (2xxx)
// ============================================================================
#define IDC_HEADER            2000    // 日期 + 总时长静态文本
#define IDC_BTN_PREV_DAY      2001    // ◀  前一天按钮
#define IDC_BTN_NEXT_DAY      2002    // ▶  后一天按钮
#define IDC_BTN_TODAY         2003    // [Today] 按钮
#define IDC_LISTVIEW          2004    // 主 ListView 控件
#define IDC_BTN_REFRESH       2005    // [Refresh] 按钮
#define IDC_BTN_IGNORE        2006    // [Ignored Apps...] 按钮
#define IDC_BTN_SETTINGS      2007    // [Settings...] 按钮
#define IDC_BTN_CLOSE         2008    // [Close] 按钮

// ---- ListView 右键菜单 ----
#define IDM_LV_ADD_IGNORE     2100    // "Add to Ignore List"

// ============================================================================
// Phase 4: 忽略列表对话框 (3xxx)
// ============================================================================
#define IDC_IGNORE_LISTBOX    3001    // 忽略项 ListBox
#define IDC_BTN_IGN_REMOVE    3002    // [Remove Selected]
#define IDC_BTN_IGN_CLOSE     3003    // [Close]
#define IDC_STATIC_IGNORE     3004    // 提示文本

// ============================================================================
// Phase 4: 设置对话框 (4xxx)
// ============================================================================
#define IDC_CHK_AUTOSTART     4001    // ☑ Start with Windows
#define IDC_EDIT_POLLING      4002    // Polling 数字输入
#define IDC_SLIDER_POLLING    4003    // Polling Trackbar
#define IDC_EDIT_IDLE         4004    // Idle 数字输入
#define IDC_SLIDER_IDLE       4005    // Idle Trackbar
#define IDC_CHK_IDLE          4006    // ☑ Enable Idle Detection
#define IDC_COMBO_RETENTION   4007    // Data Retention Combo
#define IDC_BTN_SAVE          4008    // [Save]
#define IDC_BTN_CANCEL        4009    // [Cancel]
