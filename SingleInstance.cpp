// TimeTrack - 单实例保证实现
// CreateMutexW + ERROR_ALREADY_EXISTS 判定
// 非首个实例时查找已有窗口并激活

#include "SingleInstance.h"
#include <string>

// ============================================================================
// 析构 — 释放 Mutex
// ============================================================================

SingleInstance::~SingleInstance() {
    if (m_hMutex) {
        ReleaseMutex(m_hMutex);
        CloseHandle(m_hMutex);
        m_hMutex = nullptr;
    }
}

// ============================================================================
// TryAcquire — 尝试成为首个实例
// ============================================================================

bool SingleInstance::TryAcquire(const std::wstring& mutexName) {
    // CreateMutexW: bInitialOwner=TRUE 确保创建者立即持有
    m_hMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());

    if (!m_hMutex) {
        // 创建失败（极罕见，可能权限不足）
        return false;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 已有实例在运行
        CloseHandle(m_hMutex);
        m_hMutex = nullptr;
        m_isFirst = false;

        // 查找并激活已有实例的窗口
        // 窗口类名在 main.cpp 中注册为 "TimeTrack_MainWindow"
        HWND hwndExisting = FindWindowW(L"TimeTrack_MainWindow", nullptr);
        if (hwndExisting) {
            // 若窗口最小化，先恢复
            if (IsIconic(hwndExisting)) {
                ShowWindow(hwndExisting, SW_RESTORE);
            }
            // 将已有实例置于前台
            SetForegroundWindow(hwndExisting);
        }
        return true; // 操作成功但非首个实例
    }

    // 首个实例，持有 Mutex
    m_isFirst = true;
    return true;
}
