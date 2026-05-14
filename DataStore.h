// TimeTrack - 数据持久化模块
// 职责：所有数据通过单一 JSON 文件（%APPDATA%\TimeTrack\timetrack_data.json）持久化
// 支持：每日数据读写、配置读写、原子写入防损坏、旧数据清理
// JSON 结构（单文件）：
//   {
//     "config": { ... AppConfig ... },
//     "days": {
//       "2026-05-10": { "totalSeconds": ..., "entries": [...] },
//       ...
//     }
//   }

#pragma once
#include "Types.h"
#include "JsonHelper.h"
#include <string>
#include <vector>

class DataStore {
public:
    DataStore() = default;

    // 设置数据目录（如 %APPDATA%\TimeTrack）
    void SetDataDirectory(const std::wstring& dir);
    const std::wstring& GetDataDirectory() const { return m_dataDir; }

    // ---- 整体加载/保存 ----
    // 从 timetrack_data.json 加载全部数据到内存
    bool Load();
    // 将内存中的全部数据原子写入 timetrack_data.json
    bool Save() const;

    // ---- 每日数据 ----
    // 保存/更新单日数据（调用后需显式 Save 或在 Flush 时统一保存）
    // 注意：SaveDailyData 只更新内存，不写磁盘（由 TimerEngine::Flush 统一触发写入）
    void SaveDailyData(const DailyData& data);

    // 加载指定日期的数据
    DailyData LoadDailyData(const std::string& date) const;

    // 列出所有已保存的日期
    std::vector<std::string> ListSavedDates() const;

    // 删除指定日期的数据（需显式 Save）
    void DeleteDailyData(const std::string& date);

    // ---- 配置 ----
    // 保存配置到内存（需显式调用 Save() 持久化到磁盘）
    bool SaveConfig(const AppConfig& config);

    // 从内存加载配置
    AppConfig LoadConfig() const;

    // ---- 清理 ----
    // 删除超过保留天数的旧数据（调用后自动 Save）
    void CleanOldData(int retentionDays);

    // ---- 路径 ----
    // 获取数据文件完整路径
    std::wstring GetFilePath() const;

    // 是否已成功从磁盘加载过数据
    bool IsLoaded() const { return m_loaded; }

private:
    // 序列化/反序列化 DailyData ↔ JsonValue
    static JsonValue DailyDataToJson(const DailyData& data);
    static DailyData JsonToDailyData(const std::string& date, const JsonValue& jv);

    // 序列化/反序列化 AppConfig ↔ JsonValue
    static JsonValue ConfigToJson(const AppConfig& config);
    static AppConfig JsonToConfig(const JsonValue& jv);

    // 确保数据目录存在（可能因 fallback 修改 m_dataDir，使用 mutable 保持逻辑 const）
    bool EnsureDirectory() const;

    // 原子写入：先写 .tmp，再 MoveFileEx 替换
    bool AtomicWrite(const std::wstring& filePath, const std::string& content) const;

    mutable std::wstring m_dataDir;  // mutable: EnsureDirectory 可在 const 方法中修正 fallback 路径
    JsonValue    m_document;        // 全量内存数据
    bool         m_loaded = false;
};
