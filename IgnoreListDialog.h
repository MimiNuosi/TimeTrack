// TimeTrack - 忽略列表管理对话框
// 模态对话框：ListBox 列出所有已忽略应用，支持移除

#pragma once
#include <windows.h>

// 前向声明
class IgnoreManager;
class ConfigManager;
class DataStore;
class TimerEngine;

// 显示忽略列表对话框
// parent: 父窗口句柄
// hInst:  实例句柄
// im, cm, ds, engine: 各模块指针，用于移除操作后的同步
void ShowIgnoreListDialog(HWND parent, HINSTANCE hInst,
    IgnoreManager* im, ConfigManager* cm, DataStore* ds,
    TimerEngine* engine);
