#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{

struct Json
{
    using Object = std::map<std::string, Json>;
    using Array = std::vector<Json>;

    std::variant<std::nullptr_t, bool, double, std::string, Object, Array> value;

    [[nodiscard]] bool is_null() const { return std::holds_alternative<std::nullptr_t>(value); }
    [[nodiscard]] bool is_bool() const { return std::holds_alternative<bool>(value); }
    [[nodiscard]] bool is_number() const { return std::holds_alternative<double>(value); }
    [[nodiscard]] bool is_string() const { return std::holds_alternative<std::string>(value); }
    [[nodiscard]] bool is_object() const { return std::holds_alternative<Object>(value); }
    [[nodiscard]] bool is_array() const { return std::holds_alternative<Array>(value); }

    [[nodiscard]] const Object* as_object() const { return std::get_if<Object>(&value); }
    [[nodiscard]] const Array* as_array() const { return std::get_if<Array>(&value); }
    [[nodiscard]] const std::string* as_string() const { return std::get_if<std::string>(&value); }
    [[nodiscard]] const double* as_number() const { return std::get_if<double>(&value); }
};

struct JsonParser
{
    std::string_view input;
    std::size_t pos = 0;

    [[nodiscard]] bool eof() const { return pos >= input.size(); }

    void skip_ws()
    {
        while (!eof() && std::isspace(static_cast<unsigned char>(input[pos])))
        {
            ++pos;
        }
    }

    bool consume(char expected)
    {
        skip_ws();
        if (eof() || input[pos] != expected)
        {
            return false;
        }
        ++pos;
        return true;
    }

    std::optional<Json> parse_value()
    {
        skip_ws();
        if (eof())
        {
            return std::nullopt;
        }
        const char c = input[pos];
        if (c == 'n')
        {
            if (input.substr(pos, 4) == "null")
            {
                pos += 4;
                return Json{std::nullptr_t{}};
            }
            return std::nullopt;
        }
        if (c == 't')
        {
            if (input.substr(pos, 4) == "true")
            {
                pos += 4;
                return Json{true};
            }
            return std::nullopt;
        }
        if (c == 'f')
        {
            if (input.substr(pos, 5) == "false")
            {
                pos += 5;
                return Json{false};
            }
            return std::nullopt;
        }
        if (c == '"')
        {
            return parse_string();
        }
        if (c == '{')
        {
            return parse_object();
        }
        if (c == '[')
        {
            return parse_array();
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
        {
            return parse_number();
        }
        return std::nullopt;
    }

    std::optional<Json> parse_string()
    {
        if (!consume('"'))
        {
            return std::nullopt;
        }
        std::string out;
        while (!eof())
        {
            const char c = input[pos++];
            if (c == '"')
            {
                return Json{out};
            }
            if (c == '\\')
            {
                if (eof())
                {
                    return std::nullopt;
                }
                const char esc = input[pos++];
                switch (esc)
                {
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '/':
                    out.push_back('/');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    return std::nullopt;
                }
            }
            else
            {
                out.push_back(c);
            }
        }
        return std::nullopt;
    }

    std::optional<Json> parse_number()
    {
        skip_ws();
        std::size_t start = pos;
        const bool has_leading_minus = !eof() && input[pos] == '-';
        if (has_leading_minus)
        {
            ++pos;
        }
        while (!eof() && std::isdigit(static_cast<unsigned char>(input[pos])))
        {
            ++pos;
        }
        if (!eof() && input[pos] == '.')
        {
            ++pos;
            while (!eof() && std::isdigit(static_cast<unsigned char>(input[pos])))
            {
                ++pos;
            }
        }
        if (!eof() && (input[pos] == 'e' || input[pos] == 'E'))
        {
            ++pos;
            const bool has_exp_sign = !eof() && (input[pos] == '+' || input[pos] == '-');
            if (has_exp_sign)
            {
                ++pos;
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(input[pos])))
            {
                ++pos;
            }
        }
        const std::string num(input.substr(start, pos - start));
        char* end_ptr = nullptr;
        const double value = std::strtod(num.c_str(), &end_ptr);
        if (end_ptr == num.c_str())
        {
            return std::nullopt;
        }
        return Json{value};
    }

    std::optional<Json> parse_array()
    {
        if (!consume('['))
        {
            return std::nullopt;
        }
        Json::Array items;
        skip_ws();
        if (consume(']'))
        {
            return Json{items};
        }
        while (true)
        {
            auto value = parse_value();
            if (!value.has_value())
            {
                return std::nullopt;
            }
            items.push_back(std::move(*value));
            skip_ws();
            if (consume(']'))
            {
                break;
            }
            if (!consume(','))
            {
                return std::nullopt;
            }
        }
        return Json{items};
    }

    std::optional<Json> parse_object()
    {
        if (!consume('{'))
        {
            return std::nullopt;
        }
        Json::Object obj;
        skip_ws();
        if (consume('}'))
        {
            return Json{obj};
        }
        while (true)
        {
            auto key_val = parse_string();
            if (!key_val.has_value())
            {
                return std::nullopt;
            }
            if (!key_val->is_string()) // GCOVR_EXCL_LINE
            {
                return std::nullopt; // GCOVR_EXCL_LINE
            }
            const auto key = *key_val->as_string();
            if (!consume(':'))
            {
                return std::nullopt;
            }
            auto val = parse_value();
            if (!val.has_value())
            {
                return std::nullopt;
            }
            obj.emplace(key, std::move(*val));
            skip_ws();
            if (consume('}'))
            {
                break;
            }
            if (!consume(','))
            {
                return std::nullopt;
            }
        }
        return Json{obj};
    }
};

std::optional<Json> parse_json(std::string_view input)
{
    JsonParser parser{input};
    auto result = parser.parse_value();
    if (!result.has_value())
    {
        return std::nullopt;
    }
    parser.skip_ws();
    if (!parser.eof())
    {
        return std::nullopt;
    }
    return result;
}

std::string json_escape(std::string_view input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
} // GCOVR_EXCL_LINE

std::string json_serialize(const Json& value)
{
    if (value.is_null())
    {
        return "null";
    }
    if (value.is_bool())
    {
        return std::get<bool>(value.value) ? "true" : "false";
    }
    if (value.is_number())
    {
        std::ostringstream oss;
        oss << std::get<double>(value.value);
        return oss.str();
    }
    if (value.is_string())
    {
        return "\"" + json_escape(*value.as_string()) + "\"";
    }
    if (value.is_array())
    {
        const auto& arr = *value.as_array();
        std::string out = "[";
        for (std::size_t i = 0; i < arr.size(); ++i)
        {
            if (i > 0)
            {
                out += ',';
            }
            out += json_serialize(arr[i]);
        }
        out += "]";
        return out;
    }
    const auto& obj = *value.as_object();
    std::string out = "{";
    bool first = true;
    for (const auto& [key, val] : obj)
    {
        if (!first)
        {
            out += ',';
        }
        first = false;
        out += "\"" + json_escape(key) + "\":" + json_serialize(val);
    }
    out += "}";
    return out;
}

std::optional<std::string> json_get_string(const Json::Object& obj, const std::string& key)
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->second.is_string())
    {
        return std::nullopt;
    }
    return *it->second.as_string();
}

std::optional<double> json_get_number(const Json::Object& obj, const std::string& key)
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->second.is_number())
    {
        return std::nullopt;
    }
    return *it->second.as_number();
}

std::optional<Json> json_get_object(const Json::Object& obj, const std::string& key)
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->second.is_object())
    {
        return std::nullopt;
    }
    return it->second;
}

[[nodiscard]] bool is_integral(double x)
{
    return std::isfinite(x) && std::floor(x) == x;
}

Json make_error_response(std::string id, std::string kind, std::string message)
{
    Json::Object err;
    err.emplace("kind", Json{std::move(kind)});
    err.emplace("message", Json{std::move(message)});
    err.emplace("retryable", Json{false});

    Json::Object top;
    top.emplace("id", Json{std::move(id)});
    top.emplace("ok", Json{false});
    top.emplace("protocol_version", Json{1.0});
    top.emplace("error", Json{err});
    return Json{top};
}

Json make_success_response(std::string id, std::string value)
{
    Json::Object result;
    result.emplace("type", Json{std::string("string")});
    result.emplace("value", Json{std::move(value)});

    Json::Object top;
    top.emplace("id", Json{std::move(id)});
    top.emplace("ok", Json{true});
    top.emplace("protocol_version", Json{1.0});
    top.emplace("result", Json{result});
    return Json{top};
}

} // namespace

int main()
{
    std::string line;
    if (!std::getline(std::cin, line))
    {
        const auto resp = make_error_response("", "invalid_request", "empty input");
        std::cout << json_serialize(resp) << "\n";
        return 2;
    }

    const auto parsed = parse_json(line);
    if (!parsed.has_value() || !parsed->is_object())
    {
        const auto resp = make_error_response("", "invalid_request", "malformed json");
        std::cout << json_serialize(resp) << "\n";
        return 2;
    }

    const auto& obj = *parsed->as_object();

    std::string id;
    const auto id_val = json_get_string(obj, "id");
    if (id_val.has_value())
    {
        id = *id_val;
    }

    const auto v = json_get_number(obj, "protocol_version");
    if (!v.has_value() || !is_integral(*v) || static_cast<int>(*v) != 1)
    {
        const auto resp =
            make_error_response(id, "protocol_version_unsupported", "unsupported protocol version");
        std::cout << json_serialize(resp) << "\n";
        return 2;
    }

    const auto op = json_get_string(obj, "op");
    if (!op.has_value())
    {
        const auto resp = make_error_response(id, "invalid_request", "missing op");
        std::cout << json_serialize(resp) << "\n";
        return 2;
    }

    if (*op == "handshake")
    {
        const auto resp = make_success_response(id, "ok");
        std::cout << json_serialize(resp) << "\n";
        return 0;
    }

    if (*op == "echo")
    {
        const auto echo_obj = json_get_object(obj, "echo");
        const bool echo_obj_ok = echo_obj.has_value() && echo_obj->is_object(); // GCOVR_EXCL_LINE
        if (!echo_obj_ok)
        {
            const auto resp = make_error_response(id, "invalid_request", "missing echo payload");
            std::cout << json_serialize(resp) << "\n";
            return 2;
        }
        const auto payload = json_get_string(*echo_obj->as_object(), "value");
        if (!payload.has_value())
        {
            const auto resp =
                make_error_response(id, "invalid_request", "echo.value must be string");
            std::cout << json_serialize(resp) << "\n";
            return 2;
        }
        const auto resp = make_success_response(id, *payload);
        std::cout << json_serialize(resp) << "\n";
        return 0;
    }

    const auto resp = make_error_response(id, "invalid_request", "unknown op");
    std::cout << json_serialize(resp) << "\n";
    return 2;
}
