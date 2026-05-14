// TimeTrack - 忽略列表管理实现
// 使用 unordered_map 提供 O(1) 查找
// 路径归一化：转小写（英文大写与小写路径在 Windows 上等价）

#include "IgnoreManager.h"
#include <cwctype>
#include <algorithm>

// ============================================================================
// Normalize — 路径归一化
// ============================================================================

std::wstring IgnoreManager::Normalize(const std::wstring& path) {
    std::wstring result = path;
    // 转小写
    for (auto& c : result) {
        c = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
    }
    // 去除末尾反斜杠
    while (!result.empty() && (result.back() == L'\\' || result.back() == L'/')) {
        result.pop_back();
    }
    return result;
}

// ============================================================================
// LoadFrom — 从配置加载忽略列表
// ============================================================================

void IgnoreManager::LoadFrom(const std::vector<IgnoreEntry>& ignoreList) {
    m_ignoreMap.clear();
    for (const auto& entry : ignoreList) {
        m_ignoreMap[Normalize(entry.appPath)] = entry.addedAt;
    }
}

// ============================================================================
// Add — 添加忽略项
// ============================================================================

bool IgnoreManager::Add(const std::wstring& appPath) {
    std::wstring key = Normalize(appPath);
    if (key.empty()) return false;
    if (m_ignoreMap.find(key) != m_ignoreMap.end()) {
        return false; // 已存在
    }
    m_ignoreMap[key] = std::chrono::system_clock::now();
    return true;
}

// ============================================================================
// Remove — 移除忽略项
// ============================================================================

bool IgnoreManager::Remove(const std::wstring& appPath) {
    std::wstring key = Normalize(appPath);
    return m_ignoreMap.erase(key) > 0;
}

// ============================================================================
// IsIgnored — 查询是否被忽略
// ============================================================================

bool IgnoreManager::IsIgnored(const std::wstring& appPath) const {
    std::wstring key = Normalize(appPath);
    return m_ignoreMap.find(key) != m_ignoreMap.end();
}

// ============================================================================
// GetList — 获取忽略列表（写回配置用）
// ============================================================================

std::vector<IgnoreEntry> IgnoreManager::GetList() const {
    std::vector<IgnoreEntry> result;
    result.reserve(m_ignoreMap.size());
    for (const auto& [path, addedAt] : m_ignoreMap) {
        result.push_back({ path, addedAt });
    }
    return result;
}

// ============================================================================
// Clear — 清空所有忽略项
// ============================================================================

void IgnoreManager::Clear() {
    m_ignoreMap.clear();
}
