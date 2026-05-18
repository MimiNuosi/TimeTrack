// TimeTrack - 忽略列表对话框实现
// 程序化创建的模态对话框：ListBox + Remove Selected + Close

#include "IgnoreListDialog.h"
#include "IgnoreManager.h"
#include "ConfigManager.h"
#include "DataStore.h"
#include "TimerEngine.h"
#include "Resource.h"
#include <vector>
#include <string>
#include <map>
#include <windowsx.h>

// ---- 对话框尺寸 ----
static const int DIALOG_W = 400;
static const int DIALOG_H = 330;
static const int MARGIN = 10;
static const int BTN_W = 85;
static const int BTN_H = 28;
static const int BTN_REMOVE_W = 135;  // "Remove Selected" 文字较长，单独宽度

// 对话框背景画刷（轻量浅灰）
static HBRUSH g_hIgnoreDlgBrush = nullptr;

// ---- 传递给对话框的参数 ----
struct IgnoreDialogParams {
    IgnoreManager* im;
    ConfigManager* cm;
    DataStore*     ds;
    TimerEngine*   engine;
    // 存储 appPath（按 ListBox 顺序）
    std::vector<std::wstring> appPaths;
};

// ============================================================================
// IgnoreListWndProc — 自定义窗口过程
// ============================================================================

LRESULT CALLBACK IgnoreListWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    IgnoreDialogParams* params = (IgnoreDialogParams*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
    HWND hListBox = GetDlgItem(hDlg, IDC_IGNORE_LISTBOX);

    switch (msg) {

    case WM_COMMAND:
        switch (LOWORD(wParam)) {

        case IDC_BTN_IGN_REMOVE:
        {
            if (!params || !params->im) break;

            int sel = (int)SendMessageW(hListBox, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)params->appPaths.size()) {
                MessageBoxW(hDlg, L"Please select an application to remove.",
                    L"Ignored Applications", MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            std::wstring appPath = params->appPaths[sel];

            // 从 IgnoreManager 移除
            params->im->Remove(appPath);

            // 同步 ConfigManager
            params->cm->RemoveIgnoredApp(appPath);
            params->cm->Save(*params->ds);
            params->ds->Save();

            // 通知 TimerEngine
            if (params->engine) {
                // 移除忽略意味着 app 不再被忽略，恢复可追踪
                // 但不需要特殊通知——下一Tick 时自动恢复
            }

            // 刷新列表
            params->appPaths.clear();
            SendMessageW(hListBox, LB_RESETCONTENT, 0, 0);

            auto ignoreList = params->im->GetList();
            for (const auto& entry : ignoreList) {
                std::wstring display;
                size_t pos = entry.appPath.find_last_of(L"\\/");
                if (pos != std::wstring::npos)
                    display = entry.appPath.substr(pos + 1);
                else
                    display = entry.appPath;
                SendMessageW(hListBox, LB_ADDSTRING, 0, (LPARAM)display.c_str());
                params->appPaths.push_back(entry.appPath);
            }

            if (ignoreList.empty()) {
                SendMessageW(hListBox, LB_ADDSTRING, 0,
                    (LPARAM)L"(no ignored applications)");
            }
            return 0;
        }

        case IDC_BTN_IGN_CLOSE:
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
// ShowIgnoreListDialog — 公开接口
// ============================================================================

void ShowIgnoreListDialog(HWND parent, HINSTANCE hInst,
    IgnoreManager* im, ConfigManager* cm, DataStore* ds,
    TimerEngine* engine)
{
    // 注册对话框类
    static bool classRegistered = false;
    if (!classRegistered) {
        if (!g_hIgnoreDlgBrush) g_hIgnoreDlgBrush = CreateSolidBrush(RGB(245, 245, 245));
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(WNDCLASSEXW);
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = g_hIgnoreDlgBrush;
        wc.lpszClassName = L"TimeTrack_IgnoreDlg";
        wc.lpfnWndProc   = DefWindowProcW; // 稍后子类化
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    // 对话框参数
    IgnoreDialogParams params;
    params.im     = im;
    params.cm     = cm;
    params.ds     = ds;
    params.engine = engine;

    // 构建初始列表
    auto ignoreList = im->GetList();
    for (const auto& entry : ignoreList) {
        params.appPaths.push_back(entry.appPath);
    }

    // 创建窗口
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"TimeTrack_IgnoreDlg",
        L"Ignored Applications",
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

    // 子类化窗口过程
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)&params);
    SetWindowLongPtrW(hDlg, GWLP_WNDPROC, (LONG_PTR)IgnoreListWndProc);

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // 提示标签
    HWND hLabel = CreateWindowExW(0, L"STATIC",
        L"Applications excluded from tracking:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN, MARGIN, DIALOG_W - 2 * MARGIN, 18,
        hDlg, nullptr, hInst, nullptr);
    SendMessageW(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    // ListBox — 高度从剩余空间计算，为底部按钮留足空间
    int lvH = DIALOG_H - MARGIN * 4 - BTN_H - 35;
    HWND hListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
        MARGIN, MARGIN + 20,
        DIALOG_W - 2 * MARGIN, lvH,
        hDlg, (HMENU)IDC_IGNORE_LISTBOX, hInst, nullptr);
    SendMessageW(hListBox, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 填充列表
    for (const auto& entry : ignoreList) {
        std::wstring display;
        size_t pos = entry.appPath.find_last_of(L"\\/");
        display = (pos != std::wstring::npos) ? entry.appPath.substr(pos + 1) : entry.appPath;
        SendMessageW(hListBox, LB_ADDSTRING, 0, (LPARAM)display.c_str());
    }
    if (ignoreList.empty()) {
        SendMessageW(hListBox, LB_ADDSTRING, 0, (LPARAM)L"(no ignored applications)");
    }

    int btnY = lvH + MARGIN + 25;

    // [Remove Selected] — 紧邻 [Close] 左侧，右对齐布局
    int btnRemoveX = DIALOG_W - MARGIN - BTN_W - 8 - BTN_REMOVE_W;
    HWND hBtnRemove = CreateWindowExW(0, L"BUTTON", L"Remove Selected",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_FLAT,
        btnRemoveX, btnY, BTN_REMOVE_W, BTN_H,
        hDlg, (HMENU)IDC_BTN_IGN_REMOVE, hInst, nullptr);
    SendMessageW(hBtnRemove, WM_SETFONT, (WPARAM)hFont, TRUE);

    // [Close] — 最右侧
    HWND hBtnClose = CreateWindowExW(0, L"BUTTON", L"Close",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_FLAT,
        DIALOG_W - BTN_W - MARGIN, btnY, BTN_W, BTN_H,
        hDlg, (HMENU)IDC_BTN_IGN_CLOSE, hInst, nullptr);
    SendMessageW(hBtnClose, WM_SETFONT, (WPARAM)hFont, TRUE);

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
