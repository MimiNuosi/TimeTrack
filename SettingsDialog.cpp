// TimeTrack - 设置对话框实现
// 程序化创建的模态对话框
// 控件：Checkbox(2), Trackbar+Edit联动(2), ComboBox(1), Button(2)

#include "SettingsDialog.h"
#include "ConfigManager.h"
#include "DataStore.h"
#include "Resource.h"
#include <windowsx.h>
#include <commctrl.h>

// ---- 对话框尺寸 ----
static const int DIALOG_W = 370;
static const int DIALOG_H = 350;
static const int MARGIN_L = 15;
static const int MARGIN_TOP = 12;
static const int BTN_W = 95;
static const int BTN_H = 28;
static const int TRACKBAR_W = 180;
static const int EDIT_W = 45;
static const int LABEL_W = 90;

// 参数
struct SettingsDialogParams {
    ConfigManager* cm;
    DataStore*     ds;
    AppConfig      tempConfig;   // 临时配置（Save 才应用）
};

// ============================================================================
// SyncEditFromTrackbar — 滑块 → Edit 数字
// ============================================================================

static void SyncTrackbarToEdit(HWND hDlg, int trackbarId, int editId, bool /*isMinutes*/) {
    HWND hTrack = GetDlgItem(hDlg, trackbarId);
    HWND hEdit  = GetDlgItem(hDlg, editId);
    int pos = (int)SendMessageW(hTrack, TBM_GETPOS, 0, 0);
    SetWindowTextW(hEdit, std::to_wstring(pos).c_str());
}

// ============================================================================
// SyncEditToTrackbar — Edit 数字 → 滑块
// ============================================================================

static void SyncEditToTrackbar(HWND hDlg, int editId, int trackbarId, int minVal, int maxVal) {
    HWND hEdit  = GetDlgItem(hDlg, editId);
    HWND hTrack = GetDlgItem(hDlg, trackbarId);
    wchar_t buf[16];
    GetWindowTextW(hEdit, buf, _countof(buf));
    int val = _wtoi(buf);
    if (val < minVal) val = minVal;
    if (val > maxVal) val = maxVal;
    SendMessageW(hTrack, TBM_SETPOS, TRUE, val);
}

// ============================================================================
// SettingsWndProc — 自定义窗口过程
// ============================================================================

LRESULT CALLBACK SettingsWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDialogParams* params = (SettingsDialogParams*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (msg) {

    case WM_HSCROLL:
    {
        // Trackbar 值变化
        int trackId = GetDlgCtrlID((HWND)lParam);
        if (trackId == IDC_SLIDER_POLLING) {
            SyncTrackbarToEdit(hDlg, IDC_SLIDER_POLLING, IDC_EDIT_POLLING, false);
        } else if (trackId == IDC_SLIDER_IDLE) {
            SyncTrackbarToEdit(hDlg, IDC_SLIDER_IDLE, IDC_EDIT_IDLE, true);
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {

        case IDC_EDIT_POLLING:
            if (HIWORD(wParam) == EN_CHANGE) {
                SyncEditToTrackbar(hDlg, IDC_EDIT_POLLING, IDC_SLIDER_POLLING, 1, 10);
            }
            return 0;

        case IDC_EDIT_IDLE:
            if (HIWORD(wParam) == EN_CHANGE) {
                SyncEditToTrackbar(hDlg, IDC_EDIT_IDLE, IDC_SLIDER_IDLE, 1, 30);
            }
            return 0;

        case IDC_BTN_SAVE:
        {
            if (!params || !params->cm) break;

            // 从控件读取所有值到临时配置
            AppConfig& cfg = params->tempConfig;

            // Auto Start
            HWND hChkAuto = GetDlgItem(hDlg, IDC_CHK_AUTOSTART);
            cfg.autoStart = (Button_GetCheck(hChkAuto) == BST_CHECKED);

            // Polling Interval
            HWND hEditPoll = GetDlgItem(hDlg, IDC_EDIT_POLLING);
            wchar_t bufP[16];
            GetWindowTextW(hEditPoll, bufP, _countof(bufP));
            cfg.pollingInterval = _wtoi(bufP);
            if (cfg.pollingInterval < 1) cfg.pollingInterval = 1;
            if (cfg.pollingInterval > 10) cfg.pollingInterval = 10;

            // Idle Threshold
            HWND hEditIdle = GetDlgItem(hDlg, IDC_EDIT_IDLE);
            wchar_t bufI[16];
            GetWindowTextW(hEditIdle, bufI, _countof(bufI));
            cfg.idleThresholdMinutes = _wtoi(bufI);
            if (cfg.idleThresholdMinutes < 1) cfg.idleThresholdMinutes = 1;
            if (cfg.idleThresholdMinutes > 30) cfg.idleThresholdMinutes = 30;

            // Idle Detection Enabled
            HWND hChkIdle = GetDlgItem(hDlg, IDC_CHK_IDLE);
            cfg.idleDetectionEnabled = (Button_GetCheck(hChkIdle) == BST_CHECKED);

            // Data Retention:
            //   CB index 0 → 90 days, 1 → 180 days, 2 → Forever (retentionDays=0)
            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_RETENTION);
            int selIdx = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (selIdx == 0) cfg.retentionDays = 90;
            else if (selIdx == 1) cfg.retentionDays = 180;
            else cfg.retentionDays = 0; // Forever: CleanOldData(reten≤0) 跳过清理

            // 应用到 ConfigManager
            params->cm->SetAutoStart(cfg.autoStart);
            params->cm->SetPollingInterval(cfg.pollingInterval);
            params->cm->SetIdleThreshold(cfg.idleThresholdMinutes);
            params->cm->SetIdleDetectionEnabled(cfg.idleDetectionEnabled);
            params->cm->SetRetentionDays(cfg.retentionDays);

            // 持久化（先写配置，再整体保存）
            params->cm->Save(*params->ds);
            params->ds->Save();

            // 若 retention 非 Forever，立即清理过期历史数据
            if (cfg.retentionDays > 0) {
                params->ds->CleanOldData(cfg.retentionDays);
            }

            DestroyWindow(hDlg);
            return 0;
        }

        case IDC_BTN_CANCEL:
        case IDCANCEL:
            DestroyWindow(hDlg);
            return 0;

        } // end WM_COMMAND
        return 0;

    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

// ============================================================================
// CreateLabeledEdit — 创建 Label + Edit 并排控件
// ============================================================================

static HWND CreateLabel(HWND hParent, HINSTANCE hInst, const wchar_t* text,
    int x, int y, int w, HFONT hFont)
{
    HWND hwnd = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        x, y, w, 20, hParent, nullptr, hInst, nullptr);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    return hwnd;
}

// ============================================================================
// ShowSettingsDialog — 公开接口
// ============================================================================

void ShowSettingsDialog(HWND parent, HINSTANCE hInst,
    ConfigManager* cm, DataStore* ds)
{
    // 注册对话框类
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(WNDCLASSEXW);
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"TimeTrack_SettingsDlg";
        wc.lpfnWndProc   = DefWindowProcW;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    SettingsDialogParams params;
    params.cm = cm;
    params.ds = ds;
    params.tempConfig = cm->GetConfig(); // 拷贝当前配置

    // 创建窗口
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"TimeTrack_SettingsDlg", L"Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        DIALOG_W, DIALOG_H,
        parent, nullptr, hInst, nullptr);

    if (!hDlg) return;

    // 居中
    if (parent) {
        RECT rP, rD;
        GetWindowRect(parent, &rP);
        GetWindowRect(hDlg, &rD);
        int x = rP.left + ((rP.right - rP.left) - (rD.right - rD.left)) / 2;
        int y = rP.top + ((rP.bottom - rP.top) - (rD.bottom - rD.top)) / 2;
        SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    // 子类化
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)&params);
    SetWindowLongPtrW(hDlg, GWLP_WNDPROC, (LONG_PTR)SettingsWndProc);

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    const AppConfig& cfg = params.tempConfig;

    int y = MARGIN_TOP;

    // ---- Auto Start ----
    HWND hChkAuto = CreateWindowExW(0, L"BUTTON", L"Start with Windows",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        MARGIN_L, y, DIALOG_W - 2 * MARGIN_L, 22,
        hDlg, (HMENU)IDC_CHK_AUTOSTART, hInst, nullptr);
    SendMessageW(hChkAuto, WM_SETFONT, (WPARAM)hFont, TRUE);
    Button_SetCheck(hChkAuto, cfg.autoStart ? BST_CHECKED : BST_UNCHECKED);
    y += 35;

    // ---- Polling Interval ----
    CreateLabel(hDlg, hInst, L"Polling Interval:", MARGIN_L, y, LABEL_W, hFont);

    // Edit (数字输入)
    HWND hEditPoll = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER,
        MARGIN_L + LABEL_W + 5, y, EDIT_W, 22,
        hDlg, (HMENU)IDC_EDIT_POLLING, hInst, nullptr);
    SendMessageW(hEditPoll, WM_SETFONT, (WPARAM)hFont, TRUE);
    SetWindowTextW(hEditPoll, std::to_wstring(cfg.pollingInterval).c_str());

    // Trackbar (1-10)
    int trackX = MARGIN_L + LABEL_W + EDIT_W + 15;
    HWND hTrackPoll = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ,
        trackX, y - 2, TRACKBAR_W, 28,
        hDlg, (HMENU)IDC_SLIDER_POLLING, hInst, nullptr);
    SendMessageW(hTrackPoll, TBM_SETRANGE, TRUE, MAKELONG(1, 10));
    SendMessageW(hTrackPoll, TBM_SETPOS, TRUE, cfg.pollingInterval);
    SendMessageW(hTrackPoll, TBM_SETTICFREQ, 1, 0);

    // "sec" 后缀
    CreateLabel(hDlg, hInst, L"sec", trackX + TRACKBAR_W + 5, y, 30, hFont);
    y += 35;

    // ---- Idle Threshold ----
    CreateLabel(hDlg, hInst, L"Idle Threshold:", MARGIN_L, y, LABEL_W, hFont);

    HWND hEditIdle = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER,
        MARGIN_L + LABEL_W + 5, y, EDIT_W, 22,
        hDlg, (HMENU)IDC_EDIT_IDLE, hInst, nullptr);
    SendMessageW(hEditIdle, WM_SETFONT, (WPARAM)hFont, TRUE);
    SetWindowTextW(hEditIdle, std::to_wstring(cfg.idleThresholdMinutes).c_str());

    HWND hTrackIdle = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ,
        trackX, y - 2, TRACKBAR_W, 28,
        hDlg, (HMENU)IDC_SLIDER_IDLE, hInst, nullptr);
    SendMessageW(hTrackIdle, TBM_SETRANGE, TRUE, MAKELONG(1, 30));
    SendMessageW(hTrackIdle, TBM_SETPOS, TRUE, cfg.idleThresholdMinutes);
    SendMessageW(hTrackIdle, TBM_SETTICFREQ, 5, 0);

    CreateLabel(hDlg, hInst, L"min", trackX + TRACKBAR_W + 5, y, 30, hFont);
    y += 35;

    // ---- Enable Idle Detection ----
    HWND hChkIdle = CreateWindowExW(0, L"BUTTON", L"Enable Idle Detection",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        MARGIN_L, y, DIALOG_W - 2 * MARGIN_L, 22,
        hDlg, (HMENU)IDC_CHK_IDLE, hInst, nullptr);
    SendMessageW(hChkIdle, WM_SETFONT, (WPARAM)hFont, TRUE);
    Button_SetCheck(hChkIdle, cfg.idleDetectionEnabled ? BST_CHECKED : BST_UNCHECKED);
    y += 40;

    // ---- Data Retention ----
    CreateLabel(hDlg, hInst, L"Data Retention:", MARGIN_L, y, LABEL_W, hFont);

    HWND hCombo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        MARGIN_L + LABEL_W + 5, y, 150, 200,
        hDlg, (HMENU)IDC_COMBO_RETENTION, hInst, nullptr);
    SendMessageW(hCombo, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 填充 ComboBox 项
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"90 days");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"180 days");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Forever");

    // 选择当前值（retentionDays=0 → Forever, ≤90 → 90days, ≤180 → 180days）
    if (cfg.retentionDays == 0)
        SendMessageW(hCombo, CB_SETCURSEL, 2, 0);       // Forever
    else if (cfg.retentionDays <= 90)
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0);       // 90 days
    else if (cfg.retentionDays <= 180)
        SendMessageW(hCombo, CB_SETCURSEL, 1, 0);       // 180 days
    else
        SendMessageW(hCombo, CB_SETCURSEL, 2, 0);       // Forever (large value)

    y += 30;

    // ---- 底部按钮区域 ----
    // 从内容底部留 50px 间距，避免基于窗口总高度的错误计算
    y += 50;

    // [Save] 按钮
    int btnSaveX = DIALOG_W - 2 * BTN_W - MARGIN_L - 8;
    HWND hBtnSave = CreateWindowExW(0, L"BUTTON", L"Save",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        btnSaveX, y, BTN_W, BTN_H,
        hDlg, (HMENU)IDC_BTN_SAVE, hInst, nullptr);
    SendMessageW(hBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);

    // [Cancel] 按钮
    HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        btnSaveX + BTN_W + 8, y, BTN_W, BTN_H,
        hDlg, (HMENU)IDC_BTN_CANCEL, hInst, nullptr);
    SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 显示并进入模态循环
    ShowWindow(hDlg, SW_SHOW);
    EnableWindow(parent, FALSE);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
}
