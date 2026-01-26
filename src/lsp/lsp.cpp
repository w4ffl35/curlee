#include <curlee/diag/diagnostic.h>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/ast.h>
#include <curlee/parser/parser.h>
#include <curlee/resolver/resolver.h>
#include <curlee/source/line_map.h>
#include <curlee/source/source_file.h>
#include <curlee/types/type.h>
#include <curlee/types/type_check.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace
{

struct Json
{
    using Object = std::unordered_map<std::string, Json>;
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
        if (input[pos] == '-')
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
            if (!eof() && (input[pos] == '+' || input[pos] == '-'))
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
            if (!key_val.has_value() || !key_val->is_string())
            {
                return std::nullopt;
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
}

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

std::optional<Json> json_get_array(const Json::Object& obj, const std::string& key)
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->second.is_array())
    {
        return std::nullopt;
    }
    return it->second;
}

bool read_lsp_message(std::istream& in, std::string& payload)
{
    std::string line;
    std::size_t content_length = 0;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            break;
        }
        const std::string prefix = "Content-Length:";
        if (line.rfind(prefix, 0) == 0)
        {
            content_length = static_cast<std::size_t>(std::stoul(line.substr(prefix.size())));
        }
    }
    if (content_length == 0)
    {
        return false;
    }
    payload.resize(content_length);
    in.read(payload.data(), static_cast<std::streamsize>(content_length));
    return static_cast<std::size_t>(in.gcount()) == content_length;
}

void write_lsp_message(const std::string& payload)
{
    std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
    std::cout.flush();
}

struct Document
{
    std::string uri;
    std::string text;
};

std::string uri_to_path(std::string_view uri)
{
    constexpr std::string_view kPrefix = "file://";
    if (!uri.starts_with(kPrefix))
    {
        return std::string(uri);
    }
    std::string_view path = uri.substr(kPrefix.size());
    std::string out;
    out.reserve(path.size());
    for (std::size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == '%' && i + 2 < path.size())
        {
            const auto hex = path.substr(i + 1, 2);
            const int value = std::stoi(std::string(hex), nullptr, 16);
            out.push_back(static_cast<char>(value));
            i += 2;
        }
        else
        {
            out.push_back(path[i]);
        }
    }
    return out;
}

struct LspPosition
{
    std::size_t line = 0;
    std::size_t character = 0;
};

std::optional<std::size_t> offset_from_position(const curlee::source::LineMap& line_map,
                                                const LspPosition& pos)
{
    const auto line_index = pos.line + 1;
    if (line_index == 0 || line_index > line_map.line_count())
    {
        return std::nullopt;
    }
    const auto line_start = line_map.line_start_offset(line_index);
    return line_start + pos.character;
}

struct LspRange
{
    LspPosition start;
    LspPosition end;
};

LspRange to_lsp_range(const curlee::source::Span& span, const curlee::source::LineMap& map)
{
    const auto start_lc = map.offset_to_line_col(span.start);
    const auto end_lc = map.offset_to_line_col(span.end);
    return LspRange{.start = {start_lc.line - 1, start_lc.col - 1},
                    .end = {end_lc.line - 1, end_lc.col - 1}};
}

std::string lsp_range_to_json(const LspRange& range)
{
    std::ostringstream oss;
    oss << "{\"start\":{\"line\":" << range.start.line << ",\"character\":"
        << range.start.character << "},\"end\":{\"line\":" << range.end.line
        << ",\"character\":" << range.end.character << "}}";
    return oss.str();
}

int lsp_severity(curlee::diag::Severity sev)
{
    switch (sev)
    {
    case curlee::diag::Severity::Error:
        return 1;
    case curlee::diag::Severity::Warning:
        return 2;
    case curlee::diag::Severity::Note:
        return 3;
    }
    return 3;
}

std::vector<curlee::diag::Diagnostic> collect_diagnostics(const curlee::source::SourceFile& file)
{
    const auto lexed = curlee::lexer::lex(file.contents);
    if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
    {
        return {std::get<curlee::diag::Diagnostic>(lexed)};
    }

    const auto& toks = std::get<std::vector<curlee::lexer::Token>>(lexed);
    const auto parsed = curlee::parser::parse(toks);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
    {
        return std::get<std::vector<curlee::diag::Diagnostic>>(parsed);
    }

    const auto& program = std::get<curlee::parser::Program>(parsed);
    const auto resolved = curlee::resolver::resolve(program, file);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(resolved))
    {
        return std::get<std::vector<curlee::diag::Diagnostic>>(resolved);
    }

    const auto typed = curlee::types::type_check(program);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
    {
        return std::get<std::vector<curlee::diag::Diagnostic>>(typed);
    }

    return {};
}

struct Analysis
{
    curlee::parser::Program program;
    curlee::resolver::Resolution resolution;
    curlee::types::TypeInfo type_info;
};

std::optional<Analysis> analyze(const curlee::source::SourceFile& file)
{
    const auto lexed = curlee::lexer::lex(file.contents);
    if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
    {
        return std::nullopt;
    }

    const auto& toks = std::get<std::vector<curlee::lexer::Token>>(lexed);
    auto parsed = curlee::parser::parse(toks);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
    {
        return std::nullopt;
    }

    auto program = std::move(std::get<curlee::parser::Program>(parsed));
    const auto resolved = curlee::resolver::resolve(program, file);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(resolved))
    {
        return std::nullopt;
    }

    const auto& resolution = std::get<curlee::resolver::Resolution>(resolved);
    const auto typed = curlee::types::type_check(program);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
    {
        return std::nullopt;
    }

    const auto& type_info = std::get<curlee::types::TypeInfo>(typed);
    return Analysis{.program = std::move(program),
                    .resolution = resolution,
                    .type_info = type_info};
}

bool span_contains(const curlee::source::Span& span, std::size_t offset)
{
    return offset >= span.start && offset < span.end;
}

const curlee::parser::Expr* find_expr_at(const curlee::parser::Expr& expr,
                                        std::size_t offset,
                                        const curlee::parser::Expr* best)
{
    if (span_contains(expr.span, offset))
    {
        if (!best || expr.span.length() < best->span.length())
        {
            best = &expr;
        }
    }

    if (const auto* unary = std::get_if<curlee::parser::UnaryExpr>(&expr.node))
    {
        if (unary->rhs)
        {
            best = find_expr_at(*unary->rhs, offset, best);
        }
    }
    else if (const auto* binary = std::get_if<curlee::parser::BinaryExpr>(&expr.node))
    {
        if (binary->lhs)
        {
            best = find_expr_at(*binary->lhs, offset, best);
        }
        if (binary->rhs)
        {
            best = find_expr_at(*binary->rhs, offset, best);
        }
    }
    else if (const auto* call = std::get_if<curlee::parser::CallExpr>(&expr.node))
    {
        if (call->callee)
        {
            best = find_expr_at(*call->callee, offset, best);
        }
        for (const auto& arg : call->args)
        {
            best = find_expr_at(arg, offset, best);
        }
    }
    else if (const auto* group = std::get_if<curlee::parser::GroupExpr>(&expr.node))
    {
        if (group->inner)
        {
            best = find_expr_at(*group->inner, offset, best);
        }
    }

    return best;
}

void find_exprs_in_stmt(const curlee::parser::Stmt& stmt,
                        std::size_t offset,
                        const curlee::parser::Expr*& best)
{
    if (const auto* let_stmt = std::get_if<curlee::parser::LetStmt>(&stmt.node))
    {
        best = find_expr_at(let_stmt->value, offset, best);
    }
    else if (const auto* ret_stmt = std::get_if<curlee::parser::ReturnStmt>(&stmt.node))
    {
        if (ret_stmt->value)
        {
            best = find_expr_at(*ret_stmt->value, offset, best);
        }
    }
    else if (const auto* expr_stmt = std::get_if<curlee::parser::ExprStmt>(&stmt.node))
    {
        best = find_expr_at(expr_stmt->expr, offset, best);
    }
    else if (const auto* if_stmt = std::get_if<curlee::parser::IfStmt>(&stmt.node))
    {
        best = find_expr_at(if_stmt->cond, offset, best);
        if (if_stmt->then_block)
        {
            for (const auto& inner : if_stmt->then_block->stmts)
            {
                find_exprs_in_stmt(inner, offset, best);
            }
        }
        if (if_stmt->else_block)
        {
            for (const auto& inner : if_stmt->else_block->stmts)
            {
                find_exprs_in_stmt(inner, offset, best);
            }
        }
    }
    else if (const auto* while_stmt = std::get_if<curlee::parser::WhileStmt>(&stmt.node))
    {
        best = find_expr_at(while_stmt->cond, offset, best);
        if (while_stmt->body)
        {
            for (const auto& inner : while_stmt->body->stmts)
            {
                find_exprs_in_stmt(inner, offset, best);
            }
        }
    }
    else if (const auto* block_stmt = std::get_if<curlee::parser::BlockStmt>(&stmt.node))
    {
        if (block_stmt->block)
        {
            for (const auto& inner : block_stmt->block->stmts)
            {
                find_exprs_in_stmt(inner, offset, best);
            }
        }
    }
}

std::string diagnostics_to_json(const std::vector<curlee::diag::Diagnostic>& diags,
                               const curlee::source::LineMap& map)
{
    std::string out = "[";
    for (std::size_t i = 0; i < diags.size(); ++i)
    {
        if (i > 0)
        {
            out += ',';
        }
        const auto& d = diags[i];
        LspRange range{{0, 0}, {0, 0}};
        if (d.span)
        {
            range = to_lsp_range(*d.span, map);
        }
        out += "{";
        out += "\"range\":" + lsp_range_to_json(range);
        out += ",\"severity\":" + std::to_string(lsp_severity(d.severity));
        out += ",\"message\":\"" + json_escape(d.message) + "\"";
        out += "}";
    }
    out += "]";
    return out;
}

} // namespace

int main()
{
    std::unordered_map<std::string, Document> documents;

    std::string payload;
    while (read_lsp_message(std::cin, payload))
    {
        auto parsed = parse_json(payload);
        if (!parsed.has_value() || !parsed->is_object())
        {
            continue;
        }

        const auto& root = *parsed->as_object();
        const auto method = json_get_string(root, "method");
        const auto id_it = root.find("id");

        if (!method.has_value())
        {
            continue;
        }

        if (*method == "initialize")
        {
            Json::Object capabilities;
            capabilities["textDocumentSync"] = Json{1.0};
            capabilities["definitionProvider"] = Json{true};
            capabilities["hoverProvider"] = Json{true};

            Json::Object result;
            result["capabilities"] = Json{capabilities};

            Json::Object response;
            response["jsonrpc"] = Json{std::string("2.0")};
            if (id_it != root.end())
            {
                response["id"] = id_it->second;
            }
            response["result"] = Json{result};

            write_lsp_message(json_serialize(Json{response}));
            continue;
        }

        if (*method == "shutdown")
        {
            Json::Object response;
            response["jsonrpc"] = Json{std::string("2.0")};
            if (id_it != root.end())
            {
                response["id"] = id_it->second;
            }
            response["result"] = Json{std::nullptr_t{}};
            write_lsp_message(json_serialize(Json{response}));
            continue;
        }

        if (*method == "exit")
        {
            return 0;
        }

        if (*method == "textDocument/didOpen" || *method == "textDocument/didChange")
        {
            const auto params = json_get_object(root, "params");
            if (!params.has_value())
            {
                continue;
            }

            const auto text_doc = json_get_object(*params->as_object(), "textDocument");
            if (!text_doc.has_value())
            {
                continue;
            }

            const auto uri = json_get_string(*text_doc->as_object(), "uri");
            if (!uri.has_value())
            {
                continue;
            }

            std::string text;
            if (*method == "textDocument/didOpen")
            {
                const auto text_value = json_get_string(*text_doc->as_object(), "text");
                if (!text_value.has_value())
                {
                    continue;
                }
                text = *text_value;
            }
            else
            {
                const auto changes = json_get_array(*params->as_object(), "contentChanges");
                if (!changes.has_value() || changes->as_array()->empty())
                {
                    continue;
                }
                const auto& first = changes->as_array()->front();
                if (!first.is_object())
                {
                    continue;
                }
                const auto new_text = json_get_string(*first.as_object(), "text");
                if (!new_text.has_value())
                {
                    continue;
                }
                text = *new_text;
            }

            documents[*uri] = Document{.uri = *uri, .text = text};

            const curlee::source::SourceFile file{.path = uri_to_path(*uri), .contents = text};
            curlee::source::LineMap map(text);
            const auto diagnostics = collect_diagnostics(file);

            std::ostringstream oss;
            oss << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",";
            oss << "\"params\":{\"uri\":\"" << json_escape(*uri) << "\",";
            oss << "\"diagnostics\":" << diagnostics_to_json(diagnostics, map) << "}}";
            write_lsp_message(oss.str());
            continue;
        }

        if (*method == "textDocument/definition" || *method == "textDocument/hover")
        {
            const auto params = json_get_object(root, "params");
            if (!params.has_value())
            {
                continue;
            }
            const auto text_doc = json_get_object(*params->as_object(), "textDocument");
            const auto position = json_get_object(*params->as_object(), "position");
            if (!text_doc.has_value() || !position.has_value())
            {
                continue;
            }

            const auto uri = json_get_string(*text_doc->as_object(), "uri");
            if (!uri.has_value())
            {
                continue;
            }
            const auto line = json_get_number(*position->as_object(), "line");
            const auto character = json_get_number(*position->as_object(), "character");
            if (!line.has_value() || !character.has_value())
            {
                continue;
            }

            const auto doc_it = documents.find(*uri);
            if (doc_it == documents.end())
            {
                continue;
            }
            const auto& doc = doc_it->second;
            curlee::source::LineMap map(doc.text);
            const auto offset_opt = offset_from_position(map, LspPosition{static_cast<std::size_t>(*line),
                                                                         static_cast<std::size_t>(*character)});
            if (!offset_opt.has_value())
            {
                continue;
            }

            const curlee::source::SourceFile file{.path = uri_to_path(*uri), .contents = doc.text};
            const auto analysis = analyze(file);
            if (!analysis.has_value())
            {
                continue;
            }

            if (*method == "textDocument/definition")
            {
                std::optional<curlee::source::Span> target_span;
                for (const auto& use : analysis->resolution.uses)
                {
                    if (span_contains(use.span, *offset_opt))
                    {
                        for (const auto& sym : analysis->resolution.symbols)
                        {
                            if (sym.id == use.target)
                            {
                                target_span = sym.span;
                                break;
                            }
                        }
                        break;
                    }
                }

                Json::Object response;
                response["jsonrpc"] = Json{std::string("2.0")};
                if (id_it != root.end())
                {
                    response["id"] = id_it->second;
                }
                if (target_span)
                {
                    const auto range = to_lsp_range(*target_span, map);
                    response["result"] = Json{Json::Object{{"uri", Json{*uri}},
                                                          {"range", Json{Json::Object{
                                                                        {"start", Json{Json::Object{{"line", Json{static_cast<double>(range.start.line)}},
                                                                                                    {"character", Json{static_cast<double>(range.start.character)}}}}},
                                                                        {"end", Json{Json::Object{{"line", Json{static_cast<double>(range.end.line)}},
                                                                                                  {"character", Json{static_cast<double>(range.end.character)}}}}}}}}}};
                }
                else
                {
                    response["result"] = Json{std::nullptr_t{}};
                }
                write_lsp_message(json_serialize(Json{response}));
                continue;
            }

            const curlee::parser::Expr* best = nullptr;
            for (const auto& func : analysis->program.functions)
            {
                for (const auto& stmt : func.body.stmts)
                {
                    find_exprs_in_stmt(stmt, *offset_opt, best);
                }
            }

            Json::Object response;
            response["jsonrpc"] = Json{std::string("2.0")};
            if (id_it != root.end())
            {
                response["id"] = id_it->second;
            }

            if (best)
            {
                const auto it = analysis->type_info.expr_types.find(best->id);
                if (it != analysis->type_info.expr_types.end())
                {
                    const auto type_name = std::string(curlee::types::to_string(it->second));
                    Json::Object contents;
                    contents["kind"] = Json{std::string("plaintext")};
                    contents["value"] = Json{type_name};
                    Json::Object hover;
                    hover["contents"] = Json{contents};
                    response["result"] = Json{hover};
                    write_lsp_message(json_serialize(Json{response}));
                    continue;
                }
            }

            response["result"] = Json{std::nullptr_t{}};
            write_lsp_message(json_serialize(Json{response}));
            continue;
        }
    }

    return 0;
}
