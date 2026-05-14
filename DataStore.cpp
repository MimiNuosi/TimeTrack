// TimeTrack - 数据持久化实现
// 单文件 JSON 存储：%APPDATA%\TimeTrack\timetrack_data.json
// 原子写入 + 优雅降级（文件损坏时用空数据）

#include "DataStore.h"
#include <windows.h>
#include <shlobj.h>  // SHGetFolderPathW
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>

#pragma comment(lib, "shell32.lib")

// ============================================================================
// SetDataDirectory
// ============================================================================

void DataStore::SetDataDirectory(const std::wstring& dir) {
    m_dataDir = dir;
}

// ============================================================================
// GetFilePath
// ============================================================================

std::wstring DataStore::GetFilePath() const {
    return m_dataDir + L"\\timetrack_data.json";
}

// ============================================================================
// Load — 从磁盘加载（若文件不存在则创建空结构）
// ============================================================================

bool DataStore::Load() {
    EnsureDirectory();
    std::wstring filePath = GetFilePath();

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        // 文件不存在 → 初始化空文档
        m_document = JsonValue::MakeObject();
        m_document["config"] = JsonValue::MakeObject();
        m_document["days"] = JsonValue::MakeObject();
        m_loaded = true;
        return true;
    }

    // 读取整个文件
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    std::string content = buffer.str();

    if (content.empty()) {
        // 文件为空 → 空文档
        m_document = JsonValue::MakeObject();
        m_document["config"] = JsonValue::MakeObject();
        m_document["days"] = JsonValue::MakeObject();
        m_loaded = true;
        return true;
    }

    // 解析 JSON
    std::string error;
    m_document = JsonValue::Parse(content, &error);

    if (m_document.IsNull() && !content.empty() && content != "null") {
        // 解析失败：文件损坏 → 使用空文档（安全回退）
        // 保留旧文件为 .bak 以便手动恢复
        std::wstring bakPath = filePath + L".bak";
        CopyFileW(filePath.c_str(), bakPath.c_str(), FALSE);

        // 输出调试日志，方便排查（可在 DebugView 中查看）
        std::wstring dbgMsg = L"[TimeTrack] CORRUPT JSON in " + filePath
            + L" — backed up to " + bakPath + L"\nParse error: "
            + Utf8ToWString(error);
        OutputDebugStringW(dbgMsg.c_str());

        m_document = JsonValue::MakeObject();
        m_document["config"] = JsonValue::MakeObject();
        m_document["days"] = JsonValue::MakeObject();
    }

    // 确保顶层结构完整
    if (!m_document.IsObject()) {
        m_document = JsonValue::MakeObject();
    }
    if (!m_document.Contains("config")) {
        m_document["config"] = JsonValue::MakeObject();
    }
    if (!m_document.Contains("days")) {
        m_document["days"] = JsonValue::MakeObject();
    }

    m_loaded = true;
    return true;
}

// ============================================================================
// Save — 原子写入磁盘
// ============================================================================

bool DataStore::Save() const {
    if (!m_loaded) return false;
    EnsureDirectory();

    std::string json = m_document.Serialize(true); // pretty print
    return AtomicWrite(GetFilePath(), json);
}

// ============================================================================
// SaveDailyData — 更新内存中的单日数据（合并而非覆盖）
// 合并策略：
//   - 先读取 days[date] 中已有条目
//   - 对 TimerEngine 传入的每个 entry，按 appPath 匹配：
//       已有 → 用新 seconds 覆盖（TimerEngine 已负责累计）
//       新增 → 追加到已有列表
//   - totalSeconds 由 TimerEngine 维护，直接覆盖
// ============================================================================

void DataStore::SaveDailyData(const DailyData& data) {
    if (!m_loaded) return;

    // 读取已有数据（同日可能已被之前的 Flush 保存过）
    DailyData existing = LoadDailyData(data.date);
    existing.date = data.date;

    // 按 appPath 合并：新 data 中的 entry 覆盖/追加到已有数据
    for (const auto& newEntry : data.entries) {
        int idx = existing.FindEntryIndex(newEntry.appPath);
        if (idx >= 0) {
            // 已存在 → 用 TimerEngine 累计的最新值覆盖
            existing.entries[idx].seconds = newEntry.seconds;
            existing.entries[idx].displayName = newEntry.displayName;
            existing.entries[idx].lastUpdate = newEntry.lastUpdate;
        } else {
            // 新条目 → 追加
            existing.entries.push_back(newEntry);
        }
    }

    // totalSeconds 由 TimerEngine 维护，直接使用其值
    existing.totalSeconds = data.totalSeconds;

    // 写回 JSON
    JsonValue& days = m_document["days"];
    days[data.date] = DailyDataToJson(existing);
}

// ============================================================================
// LoadDailyData — 从内存加载单日数据
// ============================================================================

DailyData DataStore::LoadDailyData(const std::string& date) const {
    DailyData result;
    result.date = date;

    if (!m_loaded) return result;

    const JsonValue& days = m_document["days"];
    if (!days.Contains(date)) return result;

    return JsonToDailyData(date, days[date]);
}

// ============================================================================
// ListSavedDates — 列出所有已保存日期
// ============================================================================

std::vector<std::string> DataStore::ListSavedDates() const {
    std::vector<std::string> dates;
    if (!m_loaded) return dates;

    const JsonValue& days = m_document["days"];
    if (days.IsObject()) {
        dates = days.GetKeys();
        std::sort(dates.begin(), dates.end()); // 按日期排序
    }
    return dates;
}

// ============================================================================
// DeleteDailyData — 删除指定日期数据
// ============================================================================

void DataStore::DeleteDailyData(const std::string& date) {
    if (!m_loaded) return;
    JsonValue& days = m_document["days"];
    if (days.IsObject() && days.Contains(date)) {
        days.objectItems.erase(date);
    }
}

// ============================================================================
// SaveConfig — 更新内存中的配置并返回
// ============================================================================

bool DataStore::SaveConfig(const AppConfig& config) {
    if (!m_loaded) return false;
    m_document["config"] = ConfigToJson(config);
    return true;
}

// ============================================================================
// LoadConfig — 从内存加载配置
// ============================================================================

AppConfig DataStore::LoadConfig() const {
    if (!m_loaded) return AppConfig{};
    const JsonValue& cfg = m_document["config"];
    return JsonToConfig(cfg);
}

// ============================================================================
// CleanOldData — 清理超过保留天数的旧数据
// ============================================================================

void DataStore::CleanOldData(int retentionDays) {
    // retentionDays <= 0: 永久保留（Forever），无需清理
    if (!m_loaded || retentionDays <= 0) return;

    // 计算截止日期：当前时间 - retentionDays 天
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(24 * retentionDays);

    // 转换为 YYYY-MM-DD（字符串比较天然支持日期顺序）
    std::time_t cutoffTime = std::chrono::system_clock::to_time_t(cutoff);
    std::tm cutoffTm;
    localtime_s(&cutoffTm, &cutoffTime);
    char cutoffStr[16];
    snprintf(cutoffStr, sizeof(cutoffStr), "%04d-%02d-%02d",
        cutoffTm.tm_year + 1900, cutoffTm.tm_mon + 1, cutoffTm.tm_mday);

    std::string cutoffDate(cutoffStr);

    // 收集需要删除的日期（早于截止日期）
    std::vector<std::string> toDelete;
    JsonValue& days = m_document["days"];
    if (days.IsObject()) {
        for (const auto& [date, v] : days.objectItems) {
            if (date < cutoffDate) {
                toDelete.push_back(date);
            }
        }
    }

    if (!toDelete.empty()) {
        // 删除过期条目
        for (const auto& date : toDelete) {
            days.objectItems.erase(date);
        }
        Save(); // 清理后持久化

        // 诊断日志：输出了清理了多少天的数据（纯 wstring 拼接，零栈缓冲区）
        std::wstring dbgMsg = L"[TimeTrack] Pruned " + std::to_wstring(toDelete.size())
            + L" days older than " + Utf8ToWString(std::string(cutoffStr))
            + L" (retention=" + std::to_wstring(retentionDays) + L" days)\n";
        OutputDebugStringW(dbgMsg.c_str());
    }
}

// ============================================================================
// EnsureDirectory — 确保数据目录存在
// ============================================================================

bool DataStore::EnsureDirectory() const {
    // SHCreateDirectoryEx 递归创建目录（类似 mkdir -p）
    int result = SHCreateDirectoryExW(nullptr, m_dataDir.c_str(), nullptr);
    if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS) {
        return true;
    }

    // 主路径创建失败 → 尝试回退到当前目录（纯 wstring 拼接，零栈缓冲区）
    std::wstring dbgMsg = L"[TimeTrack] Cannot create " + m_dataDir
        + L" (err=" + std::to_wstring(result) + L"), falling back to current directory\n";
    OutputDebugStringW(dbgMsg.c_str());

    wchar_t curDir[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, curDir) == 0) {
        return false; // 完全没有可用路径
    }
    std::wstring fallbackDir = std::wstring(curDir) + L"\\TimeTrackData";
    result = SHCreateDirectoryExW(nullptr, fallbackDir.c_str(), nullptr);
    if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS) {
        m_dataDir = fallbackDir; // 切换到回退目录
        std::wstring dbgMsg2 = L"[TimeTrack] Using fallback data directory: " + m_dataDir + L"\n";
        OutputDebugStringW(dbgMsg2.c_str());
        return true;
    }

    OutputDebugStringW(L"[TimeTrack] FATAL: Cannot create any data directory\n");
    return false;
}

// ============================================================================
// AtomicWrite — 原子写入
// ============================================================================

bool DataStore::AtomicWrite(const std::wstring& filePath, const std::string& content) const {
    std::wstring tmpPath = filePath + L".tmp";

    // 步骤 1: 写入 UTF-8 BOM + JSON 到临时文件
    // 使用 Win32 CreateFileW + WriteFile（更可靠的错误处理）
    HANDLE hFile = CreateFileW(
        tmpPath.c_str(),
        GENERIC_WRITE,
        0,                  // 独占写入
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) return false;

    // RAII 自动关闭
    auto closeFile = [](HANDLE h) { CloseHandle(h); };
    (void)closeFile; // 编译器不会报 unused

    // 写入 UTF-8 BOM（可选，帮助记事本识别编码）
    // unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    // DWORD written;
    // WriteFile(hFile, bom, sizeof(bom), &written, nullptr);

    DWORD written = 0;
    BOOL writeOk = WriteFile(hFile, content.c_str(),
        static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(hFile);

    if (!writeOk || written != content.size()) {
        DeleteFileW(tmpPath.c_str()); // 清理失败文件
        return false;
    }

    // 步骤 2: 原子替换目标文件
    // MOVEFILE_REPLACE_EXISTING: 替换现有文件
    // MOVEFILE_WRITE_THROUGH:  确保写入完成才返回（防断电丢失）
    if (!MoveFileExW(tmpPath.c_str(), filePath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }

    return true;
}

// ============================================================================
// 序列化：DailyData → JsonValue
// ============================================================================

JsonValue DataStore::DailyDataToJson(const DailyData& data) {
    JsonValue obj = JsonValue::MakeObject();
    obj["totalSeconds"] = JsonValue::MakeInt(static_cast<int64_t>(data.totalSeconds));

    JsonValue entriesArr = JsonValue::MakeArray();
    for (const auto& entry : data.entries) {
        JsonValue entryObj = JsonValue::MakeObject();
        entryObj["appPath"] = JsonValue::MakeString(WStringToUtf8(entry.appPath));
        entryObj["displayName"] = JsonValue::MakeString(WStringToUtf8(entry.displayName));
        entryObj["seconds"] = JsonValue::MakeInt(static_cast<int64_t>(entry.seconds));

        // lastUpdate 格式：ISO 8601 简化版 "YYYY-MM-DDTHH:MM:SS"
        std::time_t t = std::chrono::system_clock::to_time_t(entry.lastUpdate);
        std::tm lt;
        localtime_s(&lt, &t);
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
            lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
            lt.tm_hour, lt.tm_min, lt.tm_sec);
        entryObj["lastUpdate"] = JsonValue::MakeString(buf);

        entriesArr.Push(std::move(entryObj));
    }
    obj["entries"] = std::move(entriesArr);
    return obj;
}

// ============================================================================
// 反序列化：JsonValue → DailyData
// ============================================================================

DailyData DataStore::JsonToDailyData(const std::string& date, const JsonValue& jv) {
    DailyData data;
    data.date = date;
    if (!jv.IsObject()) return data;

    data.totalSeconds = static_cast<uint64_t>(jv["totalSeconds"].GetInt(0));

    const JsonValue& entries = jv["entries"];
    if (entries.IsArray()) {
        for (size_t i = 0; i < entries.Size(); ++i) {
            const JsonValue& e = entries[i];
            AppTimeEntry entry;
            entry.appPath     = Utf8ToWString(e["appPath"].GetString());
            entry.displayName = Utf8ToWString(e["displayName"].GetString());
            entry.seconds     = static_cast<uint64_t>(e["seconds"].GetInt(0));
            entry.lastUpdate  = SystemNow(); // 时间戳重建为当前时间（历史数据中非关键）
            data.entries.push_back(std::move(entry));
        }
    }
    return data;
}

// ============================================================================
// 序列化：AppConfig → JsonValue
// ============================================================================

JsonValue DataStore::ConfigToJson(const AppConfig& config) {
    JsonValue obj = JsonValue::MakeObject();
    obj["autoStart"]            = JsonValue::MakeBool(config.autoStart);
    obj["pollingInterval"]      = JsonValue::MakeInt(config.pollingInterval);
    obj["idleThresholdMinutes"] = JsonValue::MakeInt(config.idleThresholdMinutes);
    obj["retentionDays"]        = JsonValue::MakeInt(config.retentionDays);
    obj["idleDetectionEnabled"] = JsonValue::MakeBool(config.idleDetectionEnabled);
    obj["statisticsPaused"]     = JsonValue::MakeBool(config.statisticsPaused);

    // 忽略列表
    JsonValue ignoreArr = JsonValue::MakeArray();
    for (const auto& ie : config.ignoreList) {
        JsonValue item = JsonValue::MakeObject();
        item["appPath"] = JsonValue::MakeString(WStringToUtf8(ie.appPath));

        std::time_t t = std::chrono::system_clock::to_time_t(ie.addedAt);
        std::tm lt;
        localtime_s(&lt, &t);
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
            lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
            lt.tm_hour, lt.tm_min, lt.tm_sec);
        item["addedAt"] = JsonValue::MakeString(buf);

        ignoreArr.Push(std::move(item));
    }
    obj["ignoreList"] = std::move(ignoreArr);
    return obj;
}

// ============================================================================
// 反序列化：JsonValue → AppConfig
// ============================================================================

AppConfig DataStore::JsonToConfig(const JsonValue& jv) {
    AppConfig config;
    if (!jv.IsObject()) return config;

    config.autoStart            = jv["autoStart"].GetBool(true);
    config.pollingInterval      = static_cast<int>(jv["pollingInterval"].GetInt(2));
    config.idleThresholdMinutes = static_cast<int>(jv["idleThresholdMinutes"].GetInt(5));
    config.retentionDays        = static_cast<int>(jv["retentionDays"].GetInt(90));
    config.idleDetectionEnabled = jv["idleDetectionEnabled"].GetBool(true);
    config.statisticsPaused     = jv["statisticsPaused"].GetBool(false);

    // 忽略列表
    const JsonValue& ignoreArr = jv["ignoreList"];
    if (ignoreArr.IsArray()) {
        for (size_t i = 0; i < ignoreArr.Size(); ++i) {
            const JsonValue& item = ignoreArr[i];
            IgnoreEntry ie;
            ie.appPath = Utf8ToWString(item["appPath"].GetString());
            ie.addedAt = SystemNow(); // 重建时间戳
            config.ignoreList.push_back(std::move(ie));
        }
    }

    return config;
}
