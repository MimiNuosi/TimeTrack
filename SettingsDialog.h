// TimeTrack - 设置对话框
// 程序化模态对话框：Auto Start, Polling Interval, Idle Threshold,
// Idle Detection, Data Retention, Save/Cancel

#pragma once
#include <windows.h>

class ConfigManager;
class DataStore;

// 显示设置对话框
// parent: 父窗口
// hInst:  实例句柄
// cm, ds: 配置和数据存储模块
void ShowSettingsDialog(HWND parent, HINSTANCE hInst,
    ConfigManager* cm, DataStore* ds);
