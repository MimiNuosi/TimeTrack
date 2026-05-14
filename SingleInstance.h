// TimeTrack - 单实例保证模块
// 通过命名互斥体（Named Mutex）确保仅一个实例运行
// 若已有实例，则激活其窗口并退出当前进程

#pragma once
#include <windows.h>
#include <string>

class SingleInstance {
public:
    SingleInstance() = default;
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // 尝试获取命名 Mutex
    // mutexName: 如 L"Global\\TimeTrack_SingleInstance_1.0"
    // 返回 true: 获取成功（可能是首个实例，也可能是非首个但操作完成）
    bool TryAcquire(const std::wstring& mutexName);

    // 是否为首个实例
    bool IsFirstInstance() const { return m_isFirst; }

private:
    HANDLE m_hMutex  = nullptr;
    bool   m_isFirst = false;
};
