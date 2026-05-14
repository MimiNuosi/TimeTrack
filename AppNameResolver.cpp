// TimeTrack - 友好名称解析实现
// 通过 VersionInfo 资源读取产品名 / 文件描述
// 回退到窗口标题 → 无扩展名文件名 → "Unknown Program"
// 特殊处理：ApplicationFrameHost.exe (UWP宿主)
// 诊断日志：首次遇到每个 exePath 时完整追踪解析链路

#include "AppNameResolver.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <vector>
#include <set>

#pragma comment(lib, "version.lib")

// ---- 诊断：已见过的 exePath 集合，避免重复输出 ----
static std::set<std::wstring> g_diagSeenPaths;

// 将 WORD 格式化为 4 位大写十六进制字符串（零栈缓冲区）
static std::wstring Hex4Word(WORD v) {
    static const wchar_t hex[] = L"0123456789ABCDEF";
    return { hex[(v >> 12) & 0xF], hex[(v >> 8) & 0xF], hex[(v >> 4) & 0xF], hex[v & 0xF] };
}

// ---- 诊断辅助：格式化输出一条日志 ----
static void DiagLog(const wchar_t* prefix, const wchar_t* field, const std::wstring& value) {
    std::wstring msg = prefix;
    msg += L" ";
    msg += field;
    msg += L"='";
    msg += value;
    msg += L"'\n";
    OutputDebugStringW(msg.c_str());
}

static void DiagLogStatus(const wchar_t* prefix, const wchar_t* field, bool ok, const std::wstring& value = L"") {
    std::wstring msg = prefix;
    msg += L" ";
    msg += field;
    if (ok) {
        msg += L" OK";
        if (!value.empty()) {
            msg += L" -> '";
            msg += value;
            msg += L"'";
        }
    } else {
        msg += L" FAIL";
    }
    msg += L"\n";
    OutputDebugStringW(msg.c_str());
}

// ============================================================================
// Resolve — 多级回退解析友好名称（含完整诊断）
// ============================================================================

std::wstring AppNameResolver::Resolve(
    const std::wstring& exePath,
    const std::wstring& windowTitle) const
{
    // ---- 诊断去重：首次遇到此 exePath 才输出完整追踪 ----
    bool isFirstTime = g_diagSeenPaths.insert(exePath).second;

    if (isFirstTime) {
        DiagLog(L"[AppName]", L"ENTRY exePath", exePath);
        DiagLog(L"[AppName]", L"ENTRY windowTitle", windowTitle);
    }

    std::wstring result;

    // ---- 空路径处理：尝试仅用窗口标题 ----
    if (exePath.empty()) {
        std::wstring cleaned = CleanWindowTitle(windowTitle);
        if (!cleaned.empty()) {
            if (isFirstTime) {
                DiagLog(L"[AppName]", L"Empty exePath, using cleaned title", cleaned);
            }
            result = cleaned;
        } else {
            if (isFirstTime) {
                DiagLogStatus(L"[AppName]", L"Empty exePath+title -> Unknown Program", false);
            }
            result = L"Unknown Program";
        }
        return result;
    }

    // ---- 特殊处理：ApplicationFrameHost.exe（UWP 宿主进程） ----
    // UWP 应用以 ApplicationFrameHost.exe 作为前台宿主，
    // 其窗口标题通常就是实际应用名（如 "Calculator"），
    // 跳过 VersionInfo 查询直接用标题
    {
        std::wstring fileName = ExtractBaseName(exePath);
        if (_wcsicmp(fileName.c_str(), L"ApplicationFrameHost") == 0) {
            if (isFirstTime) {
                DiagLogStatus(L"[AppName]", L"ApplicationFrameHost detected", true);
            }
            std::wstring cleaned = CleanWindowTitle(windowTitle);
            if (!cleaned.empty()) {
                if (isFirstTime) {
                    DiagLog(L"[AppName]", L"UWP using title", cleaned);
                }
                result = cleaned;
            } else {
                // 标题为空时，使用基础名
                if (isFirstTime) {
                    DiagLog(L"[AppName]", L"UWP title empty, fallback basename", fileName);
                }
                result = fileName;
            }
            return result;
        }
    }

    // 第一级：查询 VersionInfo 中的 ProductName
    std::wstring name = QueryVersionString(exePath, L"ProductName");
    if (isFirstTime) {
        DiagLogStatus(L"[AppName]", L"ProductName", !name.empty(), name);
    }
    if (!name.empty()) {
        return name;
    }

    // 第二级：查询 VersionInfo 中的 FileDescription
    name = QueryVersionString(exePath, L"FileDescription");
    if (isFirstTime) {
        DiagLogStatus(L"[AppName]", L"FileDescription", !name.empty(), name);
    }
    if (!name.empty()) {
        return name;
    }

    // 第三级：清理窗口标题
    std::wstring cleaned = CleanWindowTitle(windowTitle);
    if (isFirstTime) {
        DiagLogStatus(L"[AppName]", L"CleanedTitle", !cleaned.empty(), cleaned);
    }
    if (!cleaned.empty()) {
        return cleaned;
    }

    // 第四级：无扩展名的文件名
    std::wstring baseName = ExtractBaseName(exePath);
    if (isFirstTime) {
        DiagLogStatus(L"[AppName]", L"ExtractBaseName", !baseName.empty(), baseName);
    }
    if (!baseName.empty()) {
        return baseName;
    }

    // 第五级：最终兜底 → Unknown Program
    {
        std::wstring warn = L"[RESOLVE FAIL] exePath='";
        warn += exePath;
        warn += L"' windowTitle='";
        warn += windowTitle;
        warn += L"'\n";
        OutputDebugStringW(warn.c_str());
    }
    return L"Unknown Program";
}

// ============================================================================
// QueryVersionString — 从 VersionInfo 读取指定键值
// ============================================================================

std::wstring AppNameResolver::QueryVersionString(
    const std::wstring& exePath,
    const std::wstring& key) const
{
    // 1) 获取版本信息大小
    DWORD dummy = 0;
    DWORD verSize = GetFileVersionInfoSizeW(exePath.c_str(), &dummy);
    if (verSize == 0) return {};

    // 2) 分配缓冲区并读取 — 使用 RAII（vector 自动管理）
    std::vector<BYTE> buffer(verSize);
    if (!GetFileVersionInfoW(exePath.c_str(), 0, verSize, buffer.data())) {
        return {};
    }

    // 3) 构造查询路径
    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    } *langInfo = nullptr;
    UINT langLen = 0;

    if (!VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation",
        (LPVOID*)&langInfo, &langLen) || langLen == 0) {
        return {};
    }

    std::wstring query = L"\\StringFileInfo\\" + Hex4Word(langInfo[0].wLanguage)
                       + Hex4Word(langInfo[0].wCodePage) + L"\\" + key;

    // 4) 查询指定键
    wchar_t* strValue = nullptr;
    UINT strLen = 0;
    if (!VerQueryValueW(buffer.data(), query.c_str(), (LPVOID*)&strValue, &strLen) || !strValue) {
        return {};
    }

    return std::wstring(strValue);
}

// ============================================================================
// ExtractBaseName — 提取无扩展名的文件名
// ============================================================================

std::wstring AppNameResolver::ExtractBaseName(const std::wstring& path) {
    if (path.empty()) return {};
    size_t lastSlash = path.find_last_of(L"\\/");
    std::wstring filename = (lastSlash == std::wstring::npos) ? path : path.substr(lastSlash + 1);
    size_t dotPos = filename.rfind(L'.');
    if (dotPos != std::wstring::npos) {
        filename = filename.substr(0, dotPos);
    }
    return filename;
}

// ============================================================================
// StripAppNameSuffix — 去除窗口标题末尾的 " - AppName" 后缀
// ============================================================================

std::wstring AppNameResolver::StripAppNameSuffix(const std::wstring& title) {
    if (title.empty()) return {};

    static const std::wstring sep1 = L" - ";       // 普通连字符
    static const std::wstring sep2 = L" \x2014 ";  // em dash 分隔符

    for (const auto& sep : { sep1, sep2 }) {
        size_t pos = title.rfind(sep);
        if (pos != std::wstring::npos && pos > 0) {
            std::wstring prefix = title.substr(0, pos);
            while (!prefix.empty() && prefix.back() == L' ') {
                prefix.pop_back();
            }
            if (!prefix.empty()) {
                return prefix;
            }
        }
    }
    return title;
}

// ============================================================================
// CleanWindowTitle — 清理窗口标题中的冗余后缀并限制长度
// ============================================================================

std::wstring AppNameResolver::CleanWindowTitle(const std::wstring& title) {
    if (title.empty()) return {};

    // 1) 去除末尾的应用名后缀（如 " - Google Chrome"）
    std::wstring stripped = StripAppNameSuffix(title);
    if (stripped.empty()) return {};

    // 2) 限制最大长度
    const size_t maxLen = 128;
    if (stripped.length() > maxLen) {
        size_t cut = maxLen;
        while (cut > maxLen / 2 && stripped[cut] != L' ') --cut;
        if (stripped[cut] != L' ') cut = maxLen;
        return stripped.substr(0, cut) + L"...";
    }
    return stripped;
}
