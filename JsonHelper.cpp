// TimeTrack - JSON 解析器 / 生成器实现
// 递归下降解析，支持所有 JSON 标准类型
// 错误时安全回退，不抛异常、不崩溃
#include "JsonHelper.h"
#include <windows.h>
#include <stdexcept>
#include <cmath>
#include <sstream>
#include <iomanip>

// ============================================================================
// 宽字符 ↔ UTF-8 转换
// ============================================================================

std::string WStringToUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    // WideCharToMultiByte with CP_UTF8: 先获取所需缓冲区大小
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &result[0], len, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWString(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &result[0], len);
    return result;
}

// ============================================================================
// JsonValue 工厂方法
// ============================================================================

JsonValue JsonValue::MakeNull()   { JsonValue v; v.type = Type::Null;   return v; }
JsonValue JsonValue::MakeBool(bool val) { JsonValue v; v.type = Type::Bool; v.boolValue = val; return v; }
JsonValue JsonValue::MakeNumber(double val) { JsonValue v; v.type = Type::Number; v.numberValue = val; return v; }
JsonValue JsonValue::MakeInt(int64_t val) { return MakeNumber(static_cast<double>(val)); }
JsonValue JsonValue::MakeString(const std::string& s) { JsonValue v; v.type = Type::String; v.stringValue = s; return v; }
JsonValue JsonValue::MakeObject() { JsonValue v; v.type = Type::Object; return v; }
JsonValue JsonValue::MakeArray()  { JsonValue v; v.type = Type::Array;  return v; }

// ============================================================================
// 安全取值
// ============================================================================

bool JsonValue::GetBool(bool def) const {
    return (type == Type::Bool) ? boolValue : def;
}

double JsonValue::GetNumber(double def) const {
    return (type == Type::Number) ? numberValue : def;
}

int64_t JsonValue::GetInt(int64_t def) const {
    if (type != Type::Number) return def;
    // 安全截断：仅当值是精确整数时返回
    double intPart;
    if (std::modf(numberValue, &intPart) == 0.0) {
        return static_cast<int64_t>(intPart);
    }
    return def;
}

std::string JsonValue::GetString(const std::string& def) const {
    return (type == Type::String) ? stringValue : def;
}

// ============================================================================
// Object 接口
// ============================================================================

bool JsonValue::Contains(const std::string& key) const {
    return (type == Type::Object) && (objectItems.find(key) != objectItems.end());
}

JsonValue& JsonValue::operator[](const std::string& key) {
    if (type != Type::Object) {
        type = Type::Object;
        objectItems.clear();
    }
    return objectItems[key];
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    static const JsonValue nullVal;
    if (type != Type::Object) return nullVal;
    auto it = objectItems.find(key);
    return (it != objectItems.end()) ? it->second : nullVal;
}

std::vector<std::string> JsonValue::GetKeys() const {
    std::vector<std::string> keys;
    if (type == Type::Object) {
        for (const auto& [k, v] : objectItems) {
            keys.push_back(k);
        }
    }
    return keys;
}

void JsonValue::Set(const std::string& key, JsonValue val) {
    if (type != Type::Object) {
        type = Type::Object;
        objectItems.clear();
    }
    objectItems[key] = std::move(val);
}

// ============================================================================
// Array 接口
// ============================================================================

JsonValue& JsonValue::operator[](size_t index) {
    // 越界访问：扩展数组（防御性编程，调用方应保证 index 有效）
    if (type != Type::Array) {
        type = Type::Array;
        arrayItems.clear();
    }
    if (index >= arrayItems.size()) {
        arrayItems.resize(index + 1);
    }
    return arrayItems[index];
}

const JsonValue& JsonValue::operator[](size_t index) const {
    static const JsonValue nullVal;
    if (type != Type::Array || index >= arrayItems.size()) return nullVal;
    return arrayItems[index];
}

void JsonValue::Push(JsonValue val) {
    if (type != Type::Array) {
        type = Type::Array;
        arrayItems.clear();
    }
    arrayItems.push_back(std::move(val));
}

// ============================================================================
// 序列化（JSON → 字符串）
// ============================================================================

void JsonValue::EscapeString(const std::string& src, std::string& dst) {
    dst.reserve(dst.size() + src.size() + 2);
    for (char c : src) {
        switch (c) {
        case '\"': dst += "\\\""; break;
        case '\\': dst += "\\\\"; break;
        case '\b': dst += "\\b";  break;
        case '\f': dst += "\\f";  break;
        case '\n': dst += "\\n";  break;
        case '\r': dst += "\\r";  break;
        case '\t': dst += "\\t";  break;
        default:
            // 控制字符用 \u00XX 转义
            if (static_cast<unsigned char>(c) < 0x20) {
                dst += "\\u00";
                dst += "0123456789abcdef"[(c >> 4) & 0xF];
                dst += "0123456789abcdef"[c & 0xF];
            } else {
                dst += c;
            }
        }
    }
}

void JsonValue::Indent(std::string& out, int depth, bool pretty) {
    if (pretty) {
        out += '\n';
        for (int i = 0; i < depth; ++i) out += "  ";
    }
}

std::string JsonValue::Serialize(bool pretty, int indent) const {
    std::string out;
    switch (type) {
    case Type::Null:
        out = "null";
        break;
    case Type::Bool:
        out = boolValue ? "true" : "false";
        break;
    case Type::Number: {
        // 整数不加小数点
        double intPart;
        if (std::modf(numberValue, &intPart) == 0.0 && std::isfinite(numberValue)) {
            out = std::to_string(static_cast<int64_t>(numberValue));
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.15g", numberValue);
            out = buf;
        }
        break;
    }
    case Type::String:
        out = "\"";
        EscapeString(stringValue, out);
        out += "\"";
        break;
    case Type::Object: {
        out = "{";
        bool first = true;
        for (const auto& [k, v] : objectItems) {
            if (!first) out += ",";
            first = false;
            Indent(out, indent + 1, pretty);
            out += "\"";
            EscapeString(k, out);
            out += "\":";
            if (pretty) out += " ";
            out += v.Serialize(pretty, indent + 1);
        }
        Indent(out, indent, pretty);
        out += "}";
        break;
    }
    case Type::Array: {
        out = "[";
        bool first = true;
        for (const auto& v : arrayItems) {
            if (!first) out += ",";
            first = false;
            Indent(out, indent + 1, pretty);
            out += v.Serialize(pretty, indent + 1);
        }
        Indent(out, indent, pretty);
        out += "]";
        break;
    }
    }
    return out;
}

// ============================================================================
// 反序列化（字符串 → JSON）— 递归下降解析器
// ============================================================================

namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& json, std::string* error)
        : m_input(json), m_error(error), m_pos(0) {}

    JsonValue Parse() {
        try {
            SkipWhitespace();
            if (m_pos >= m_input.size()) {
                SetError("Unexpected end of input");
                return JsonValue::MakeNull();
            }
            JsonValue result = ParseValue();
            SkipWhitespace();
            if (m_pos < m_input.size()) {
                // 顶级值之后还有多余字符，但容忍（宽松模式）
            }
            return result;
        } catch (const std::exception& ex) {
            SetError(std::string("Parse exception: ") + ex.what());
            return JsonValue::MakeNull();
        } catch (...) {
            SetError("Unknown parse error");
            return JsonValue::MakeNull();
        }
    }

private:
    // 跳过空白字符
    void SkipWhitespace() {
        while (m_pos < m_input.size()) {
            char c = m_input[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    // 查看当前字符
    char Peek() const {
        return (m_pos < m_input.size()) ? m_input[m_pos] : '\0';
    }

    // 消费当前字符并返回
    char Next() {
        return (m_pos < m_input.size()) ? m_input[m_pos++] : '\0';
    }

    // 尝试匹配字符串，匹配成功则前进并返回 true
    bool Match(const char* s) {
        size_t len = strlen(s);
        if (m_pos + len <= m_input.size() &&
            m_input.compare(m_pos, len, s) == 0) {
            m_pos += len;
            return true;
        }
        return false;
    }

    void SetError(const std::string& msg) {
        if (m_error) {
            *m_error = msg + " at position " + std::to_string(m_pos);
        }
        // 不抛异常，回退到 Null
    }

    // ---- 递归下降解析 ----

    JsonValue ParseValue() {
        SkipWhitespace();
        char c = Peek();
        switch (c) {
        case '{': return ParseObject();
        case '[': return ParseArray();
        case '\"': return ParseString();
        case 't': case 'f': return ParseBool();
        case 'n': return ParseNull();
        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return ParseNumber();
        default:
            SetError(std::string("Unexpected character: '") + c + "'");
            return JsonValue::MakeNull();
        }
    }

    JsonValue ParseObject() {
        JsonValue obj = JsonValue::MakeObject();
        Next(); // 跳过 '{'
        SkipWhitespace();

        if (Peek() == '}') {
            Next(); // 空对象
            return obj;
        }

        while (true) {
            SkipWhitespace();
            if (Peek() != '\"') {
                SetError("Expected string key in object");
                return JsonValue::MakeNull();
            }

            std::string key = ParseStringRaw();
            if (key.empty() && m_error) return JsonValue::MakeNull();

            SkipWhitespace();
            if (Next() != ':') { // 检查 ':'
                SetError("Expected ':' after object key");
                return JsonValue::MakeNull();
            }

            SkipWhitespace();
            obj.Set(key, ParseValue());

            SkipWhitespace();
            if (Peek() == '}') {
                Next();
                break;
            }
            if (Next() != ',') {
                SetError("Expected ',' or '}' in object");
                return JsonValue::MakeNull();
            }
        }
        return obj;
    }

    JsonValue ParseArray() {
        JsonValue arr = JsonValue::MakeArray();
        Next(); // 跳过 '['
        SkipWhitespace();

        if (Peek() == ']') {
            Next(); // 空数组
            return arr;
        }

        while (true) {
            SkipWhitespace();
            arr.Push(ParseValue());

            SkipWhitespace();
            if (Peek() == ']') {
                Next();
                break;
            }
            if (Next() != ',') {
                SetError("Expected ',' or ']' in array");
                return JsonValue::MakeNull();
            }
        }
        return arr;
    }

    // ParseStringRaw: 解析并返回纯字符串内容（不含引号）
    std::string ParseStringRaw() {
        if (Next() != '\"') { // 消费开引号
            SetError("Expected '\"' for string");
            return {};
        }
        std::string result;
        while (m_pos < m_input.size()) {
            char c = Next();
            if (c == '\"') {
                return result; // 字符串结束
            }
            if (c == '\\') {
                // 转义字符
                char esc = Next();
                switch (esc) {
                case '\"': result += '\"'; break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'u': {
                    // \uXXXX Unicode 转义（仅处理 BMP，即 4 位十六进制）
                    std::string hex;
                    for (int i = 0; i < 4 && m_pos < m_input.size(); ++i) {
                        hex += Next();
                    }
                    try {
                        unsigned int codepoint = (unsigned int)std::stoul(hex, nullptr, 16);
                        if (codepoint <= 0x7F) {
                            result += static_cast<char>(codepoint);
                        } else if (codepoint <= 0x7FF) {
                            result += static_cast<char>(0xC0 | (codepoint >> 6));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else {
                            result += static_cast<char>(0xE0 | (codepoint >> 12));
                            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                    } catch (...) {
                        // 无效 Unicode 转义，忽略
                    }
                    break;
                }
                default:
                    result += esc; // 未知转义，保留原字符
                    break;
                }
            } else {
                result += c;
            }
        }
        // 未闭合字符串 → 安全回退（已到达输入末尾）
        SetError("Unterminated string");
        return result;
    }

    JsonValue ParseString() {
        JsonValue val = JsonValue::MakeString("");
        val.stringValue = ParseStringRaw();
        return val;
    }

    JsonValue ParseNumber() {
        size_t start = m_pos;
        // 读取完整数字 token
        if (Peek() == '-') Next();

        // 整数部分
        if (Peek() == '0') {
            Next();
        } else if (Peek() >= '1' && Peek() <= '9') {
            while (Peek() >= '0' && Peek() <= '9') Next();
        } else if (Peek() != '.') {
            // 不是有效数字开头（在 ParseValue 中已检查，防御性保留）
            SetError("Invalid number");
            return JsonValue::MakeNull();
        }

        // 小数部分
        if (Peek() == '.') {
            Next();
            if (Peek() < '0' || Peek() > '9') {
                SetError("Expected digit after decimal point");
                return JsonValue::MakeNull();
            }
            while (Peek() >= '0' && Peek() <= '9') Next();
        }

        // 指数部分
        if (Peek() == 'e' || Peek() == 'E') {
            Next();
            if (Peek() == '+' || Peek() == '-') Next();
            if (Peek() < '0' || Peek() > '9') {
                SetError("Expected digit in exponent");
                return JsonValue::MakeNull();
            }
            while (Peek() >= '0' && Peek() <= '9') Next();
        }

        std::string numStr = m_input.substr(start, m_pos - start);
        JsonValue val = JsonValue::MakeNumber(0.0);
        try {
            val.numberValue = std::stod(numStr);
        } catch (...) {
            SetError("Invalid number format: " + numStr);
            return JsonValue::MakeNull();
        }
        return val;
    }

    JsonValue ParseBool() {
        if (Match("true"))  return JsonValue::MakeBool(true);
        if (Match("false")) return JsonValue::MakeBool(false);
        SetError("Expected 'true' or 'false'");
        return JsonValue::MakeNull();
    }

    JsonValue ParseNull() {
        if (Match("null")) return JsonValue::MakeNull();
        SetError("Expected 'null'");
        return JsonValue::MakeNull();
    }

    const std::string& m_input;
    std::string*       m_error;
    size_t             m_pos;
};

} // anonymous namespace

// 公开接口
JsonValue JsonValue::Parse(const std::string& json, std::string* errorOut) {
    JsonParser parser(json, errorOut);
    return parser.Parse();
}
