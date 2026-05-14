// TimeTrack - 自建轻量 JSON 解析器 / 生成器
// 支持类型：null, bool, number(double), string, object, array
// 不使用任何第三方库，完全自行实现
#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Object, Array };

    // ---- 数据成员（公开，简化访问） ----
    Type        type        = Type::Null;
    bool        boolValue   = false;
    double      numberValue = 0.0;
    std::string stringValue;
    // Object: key-value 对，使用 map 保证键有序（便于人工阅读 JSON）
    std::map<std::string, JsonValue> objectItems;
    // Array: 有序列表
    std::vector<JsonValue> arrayItems;

    // ---- 工厂方法 ----
    static JsonValue MakeNull();
    static JsonValue MakeBool(bool v);
    static JsonValue MakeNumber(double v);
    static JsonValue MakeInt(int64_t v);
    static JsonValue MakeString(const std::string& s);
    static JsonValue MakeObject();
    static JsonValue MakeArray();

    // ---- 类型查询 ----
    bool IsObject() const { return type == Type::Object; }
    bool IsArray()  const { return type == Type::Array;  }
    bool IsString() const { return type == Type::String; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsBool()   const { return type == Type::Bool;   }
    bool IsNull()   const { return type == Type::Null;   }

    // ---- 安全取值（带默认值，类型不匹配时返回默认值） ----
    bool          GetBool(bool def = false) const;
    double        GetNumber(double def = 0.0) const;
    int64_t       GetInt(int64_t def = 0) const;
    std::string   GetString(const std::string& def = "") const;

    // ---- Object 接口 ----
    bool          Contains(const std::string& key) const;
    JsonValue&    operator[](const std::string& key);
    const JsonValue& operator[](const std::string& key) const;
    std::vector<std::string> GetKeys() const;
    void          Set(const std::string& key, JsonValue val);

    // ---- Array 接口 ----
    size_t        Size() const { return arrayItems.size(); }
    JsonValue&    operator[](size_t index);
    const JsonValue& operator[](size_t index) const;
    void          Push(JsonValue val);
    // Range-based for support
    auto begin()       { return arrayItems.begin(); }
    auto end()         { return arrayItems.end();   }
    auto begin() const { return arrayItems.begin(); }
    auto end()   const { return arrayItems.end();   }

    // ---- 序列化 ----
    // 将 JsonValue 树序列化为 JSON 文本字符串
    std::string Serialize(bool pretty = false, int indent = 0) const;

    // ---- 反序列化 ----
    // 从 JSON 文本解析，出错时返回 Null 并设置 errorOut（若非 null）
    // 解析失败时安全回退为 Null，不会崩溃
    static JsonValue Parse(const std::string& json, std::string* errorOut = nullptr);

private:
    static void EscapeString(const std::string& src, std::string& dst);
    static void Indent(std::string& out, int depth, bool pretty);
};

// ============================================================================
// 宽字符 ↔ UTF-8 转换（Win32 API）
// ============================================================================
std::string  WStringToUtf8(const std::wstring& ws);
std::wstring Utf8ToWString(const std::string& s);
