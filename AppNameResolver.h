// TimeTrack - 友好名称解析模块
// 职责：从可执行文件路径解析为可读显示名称
// 三级回退策略：
//   1. ProductName（版 本信息）
//   2. FileDescription（版本信息）
//   3. 进程名 + 窗口标题（去尾处理后）
// 使用 GetFileVersionInfo / VerQueryValue API

#pragma once
#include <string>

class AppNameResolver {
public:
    AppNameResolver() = default;

    // 解析友好名称
    // exePath:      完整 exe 路径
    // windowTitle:  当前窗口标题（作为第三级回退）
    // 返回：可显示的友好名称
    std::wstring Resolve(const std::wstring& exePath,
                         const std::wstring& windowTitle) const;

private:
    // 从 exe 的 VersionInfo 资源中读取指定键值
    // key 示例: L"ProductName", L"FileDescription"
    std::wstring QueryVersionString(const std::wstring& exePath,
                                    const std::wstring& key) const;

    // 从文件路径提取无扩展名的文件名
    // "C:\...\chrome.exe" → "chrome"
    static std::wstring ExtractBaseName(const std::wstring& path);

    // 清理窗口标题：去除后置 " - AppName" 类冗余后缀并限制长度
    static std::wstring CleanWindowTitle(const std::wstring& title);

    // 去除窗口标题末尾的应用名后缀
    // "Page Title - Google Chrome" → "Page Title"
    // "file.ts — Visual Studio Code" → "file.ts"
    // 支持 " - " 和 " — "（em dash）两种分隔符
    static std::wstring StripAppNameSuffix(const std::wstring& title);

    // 简易三态比较：返回 true 表示不相等
    static bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) == 0;
    }
};
