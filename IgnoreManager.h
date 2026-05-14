// TimeTrack - 忽略列表管理模块
// 职责：管理被忽略的应用路径列表，提供快速 O(1) 查找
// 存储结构：unordered_map<wstring, time_point>（路径→添加时间）
// 路径归一化：统一转小写、去除末尾反斜杠

#pragma once
#include "Types.h"
#include <string>
#include <vector>
#include <unordered_map>

class IgnoreManager {
public:
    IgnoreManager() = default;

    // 从 ConfigManager 的忽略列表同步初始化
    void LoadFrom(const std::vector<IgnoreEntry>& ignoreList);

    // 添加忽略项（返回 true 表示新增）
    bool Add(const std::wstring& appPath);

    // 移除忽略项（返回 true 表示确实移除了）
    bool Remove(const std::wstring& appPath);

    // 查询某路径是否被忽略（大小写不敏感）
    bool IsIgnored(const std::wstring& appPath) const;

    // 获取当前忽略列表（用于写回 ConfigManager）
    std::vector<IgnoreEntry> GetList() const;

    // 清空所有忽略项
    void Clear();

private:
    // 路径归一化：转小写，确保一致性比较
    static std::wstring Normalize(const std::wstring& path);

    // 归一化路径 → 添加时间
    std::unordered_map<std::wstring, std::chrono::system_clock::time_point> m_ignoreMap;
};
