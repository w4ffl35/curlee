#include <cassert>
#include <curlee/diag/diagnostic.h>
#include <curlee/lexer/token.h>
#include <curlee/parser/ast.h>
#include <curlee/source/source_file.h>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

// Include the LSP implementation directly so we can unit-test internal helpers
// (JsonParser/json_serialize/pred_to_string/etc.) deterministically.
#define main curlee_lsp_main
#include "../src/lsp/lsp.cpp"
#undef main

static std::string lsp_frame(const std::string& payload)
{
    std::ostringstream oss;
    oss << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
    return oss.str();
}

int main()
{
    {
        // JsonParser: empty input.
        JsonParser p{.input = "", .pos = 0};
        if (p.parse_value().has_value())
        {
            fail("expected parse_value(empty) to fail");
        }
    }

    {
        // JsonParser: near-misses for null/true/false.
        JsonParser p{.input = "nul", .pos = 0};
        if (p.parse_value().has_value())
        {
            fail("expected parse_value('nul') to fail");
        }
        p = JsonParser{.input = "tru", .pos = 0};
        if (p.parse_value().has_value())
        {
            fail("expected parse_value('tru') to fail");
        }
        p = JsonParser{.input = "fals", .pos = 0};
        if (p.parse_value().has_value())
        {
            fail("expected parse_value('fals') to fail");
        }
    }

    {
        // JsonParser: string escape variants.
        JsonParser p{.input = "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"", .pos = 0};
        const auto v = p.parse_value();
        if (!v.has_value() || !v->is_string())
        {
            fail("expected parse_value(escaped string) to succeed");
        }
        const auto* s = v->as_string();
        if (s == nullptr || s->find('\n') == std::string::npos ||
            s->find('\t') == std::string::npos)
        {
            fail("expected parsed string to contain newline/tab characters");
        }

        // Trailing backslash should fail.
        p = JsonParser{.input = "\"abc\\\\", .pos = 0};
        if (p.parse_value().has_value())
        {
            fail("expected parse_value(trailing backslash) to fail");
        }

        // Invalid escape should fail.
        p = JsonParser{.input = "\"\\q\"", .pos = 0};
        if (p.parse_value().has_value())
        {
            fail("expected parse_value(invalid escape) to fail");
        }
    }

    {
        // JsonParser: numbers with decimals and exponent.
        JsonParser p{.input = "1.25", .pos = 0};
        const auto v = p.parse_value();
        if (!v.has_value() || !v->is_number())
        {
            fail("expected parse_value(decimal) to succeed");
        }
        p = JsonParser{.input = "1e+2", .pos = 0};
        if (!p.parse_value().has_value())
        {
            fail("expected parse_value(exp) to succeed");
        }

        // Additional number forms to hit remaining parse_number branches.
        p = JsonParser{.input = "1.", .pos = 0}; // dot with no trailing digits
        if (!p.parse_value().has_value())
        {
            fail("expected parse_value('1.') to succeed");
        }
        p = JsonParser{.input = "1E2", .pos = 0}; // uppercase exponent
        if (!p.parse_value().has_value())
        {
            fail("expected parse_value('1E2') to succeed");
        }
        p = JsonParser{.input = "1e2", .pos = 0}; // exponent without sign
        if (!p.parse_value().has_value())
        {
            fail("expected parse_value('1e2') to succeed");
        }
        p = JsonParser{.input = "1e-2", .pos = 0}; // negative exponent sign
        if (!p.parse_value().has_value())
        {
            fail("expected parse_value('1e-2') to succeed");
        }

        // Dot followed by a non-digit (invalid JSON, but exercises a distinct branch inside
        // parse_number's fractional loop condition).
        p = JsonParser{.input = "1.a", .pos = 0};
        if (!p.parse_value().has_value())
        {
            fail("expected parse_value('1.a') to succeed as a number prefix");
        }

        // Exponent marker at end (invalid JSON, but exercises eof-at-sign handling).
        p = JsonParser{.input = "1e", .pos = 0};
        if (!p.parse_value().has_value())
        {
            fail("expected parse_value('1e') to succeed as a number prefix");
        }

        // Malformed number.
        p = JsonParser{.input = "-", .pos = 0};
        if (p.parse_value().has_value())
        {
            fail("expected parse_value('-') to fail");
        }
    }

    {
        // JsonParser: arrays/objects ok + failure modes.
        JsonParser p{.input = "[]", .pos = 0};
        if (!p.parse_value().has_value())
        {
            fail("expected parse_value([]) to succeed");
        }
        p = JsonParser{.input = "[1,]", .pos = 0};
        if (p.parse_value().has_value())
        {
            fail("expected parse_value([1,]) to fail");
        }
        p = JsonParser{.input = "{\"a\":1}", .pos = 0};
        if (!p.parse_value().has_value())
        {
            fail("expected parse_value({a:1}) to succeed");
        }
        p = JsonParser{.input = "{a:1}", .pos = 0};
        if (p.parse_value().has_value())
        {
            fail("expected parse_value({a:1}) to fail due to non-string key");
        }
        p = JsonParser{.input = "{\"a\":1 \"b\":2}", .pos = 0};
        if (p.parse_value().has_value())
        {
            fail("expected parse_value(missing comma) to fail");
        }

        // Directly drive parse_array/parse_object failure-at-entry branches.
        p = JsonParser{.input = "123", .pos = 0};
        if (p.parse_array().has_value())
        {
            fail("expected parse_array(non-[) to fail");
        }
        p = JsonParser{.input = "123", .pos = 0};
        if (p.parse_object().has_value())
        {
            fail("expected parse_object(non-{) to fail");
        }
    }

    {
        // json_escape: cover special characters.
        const std::string in = std::string("\t\r\n\\\"");
        const std::string out = json_escape(in);
        if (out.find("\\t") == std::string::npos || out.find("\\r") == std::string::npos ||
            out.find("\\n") == std::string::npos || out.find("\\\\") == std::string::npos ||
            out.find("\\\"") == std::string::npos)
        {
            fail("expected json_escape to escape special characters");
        }
    }

    {
        // json_serialize: array and object cases.
        Json::Array arr;
        arr.push_back(Json{std::nullptr_t{}});
        arr.push_back(Json{true});
        arr.push_back(Json{false});
        arr.push_back(Json{1.0});
        arr.push_back(Json{std::string("x")});
        const std::string s = json_serialize(Json{arr});
        if (s.empty() || s.front() != '[' || s.back() != ']')
        {
            fail("expected json_serialize(array) to produce []");
        }

        Json::Object obj;
        obj.emplace("b", Json{1.0});
        obj.emplace("a", Json{2.0});
        const std::string so = json_serialize(Json{obj});
        if (so.find("\"a\"") == std::string::npos || so.find("\"b\"") == std::string::npos)
        {
            fail("expected json_serialize(object) to include keys");
        }
    }

    {
        // token_op_to_string and pred_to_string.
        if (token_op_to_string(curlee::lexer::TokenKind::Identifier) != "<op>")
        {
            fail("expected token_op_to_string(default) to return <op>");
        }
        if (token_op_to_string(curlee::lexer::TokenKind::Slash) != "/")
        {
            fail("expected token_op_to_string(Slash) to return /");
        }

        curlee::parser::Pred p_true{.span = curlee::source::Span{.start = 0, .end = 0},
                                    .node = curlee::parser::PredBool{.value = true}};
        if (pred_to_string(p_true) != "true")
        {
            fail("expected pred_to_string(true) to be true");
        }

        curlee::parser::Pred p_false{.span = curlee::source::Span{.start = 0, .end = 0},
                                     .node = curlee::parser::PredBool{.value = false}};
        if (pred_to_string(p_false) != "false")
        {
            fail("expected pred_to_string(false) to be false");
        }

        curlee::parser::Pred p_not_true;
        p_not_true.span = curlee::source::Span{.start = 0, .end = 0};
        p_not_true.node = curlee::parser::PredUnary{
            .op = curlee::lexer::TokenKind::Bang,
            .rhs = std::make_unique<curlee::parser::Pred>(std::move(p_true)),
        };
        if (pred_to_string(p_not_true).find('!') == std::string::npos)
        {
            fail("expected pred_to_string(!true) to include '!'");
        }

        // pred_to_string_with_subst + slice_span_text.
        curlee::parser::Pred p_name{.span = curlee::source::Span{.start = 0, .end = 1},
                                    .node = curlee::parser::PredName{.name = "x"}};
        std::unordered_map<std::string, std::string> subst;
        subst.emplace("x", "7");
        if (pred_to_string_with_subst(p_name, subst) != "7")
        {
            fail("expected pred_to_string_with_subst hit to substitute");
        }
        subst.clear();
        if (pred_to_string_with_subst(p_name, subst) != "x")
        {
            fail("expected pred_to_string_with_subst miss to return name");
        }

        const std::string_view text = "abcdef";
        if (slice_span_text(text, curlee::source::Span{.start = 1, .end = 3}) != "bc")
        {
            fail("expected slice_span_text to return substring");
        }
        if (!slice_span_text(text, curlee::source::Span{.start = 5, .end = 5}).empty())
        {
            fail("expected slice_span_text invalid span to return empty");
        }
        if (!slice_span_text(text, curlee::source::Span{.start = 999, .end = 1000}).empty())
        {
            fail("expected slice_span_text out-of-bounds span to return empty");
        }
    }

    {
        // lsp_severity.
        if (lsp_severity(curlee::diag::Severity::Error) != 1 ||
            lsp_severity(curlee::diag::Severity::Warning) != 2 ||
            lsp_severity(curlee::diag::Severity::Note) != 3)
        {
            fail("unexpected lsp_severity mapping");
        }

        // Drive the post-switch default return for invalid enum values.
        const auto bogus = static_cast<curlee::diag::Severity>(999);
        if (lsp_severity(bogus) != 3)
        {
            fail("expected lsp_severity(bogus) to fall back to 3");
        }
    }

    {
        // uri_to_path: prefix and percent-decoding.
        if (uri_to_path("/tmp/x.curlee") != "/tmp/x.curlee")
        {
            fail("expected uri_to_path(non-file) to pass through");
        }
        const std::string decoded = uri_to_path("file:///tmp/a%20b.curlee");
        if (decoded.find("a b.curlee") == std::string::npos)
        {
            fail("expected uri_to_path to decode %20");
        }

        const std::string trailing_percent = uri_to_path("file:///tmp/a%");
        if (trailing_percent.find('%') == std::string::npos)
        {
            fail("expected uri_to_path to keep trailing %");
        }
    }

    {
        // json_get_* helpers: missing keys and wrong types.
        Json::Object obj;
        obj.emplace("s", Json{std::string("hi")});
        obj.emplace("n", Json{1.0});
        obj.emplace("a", Json{Json::Array{}});
        obj.emplace("o", Json{Json::Object{}});

        if (json_get_string(obj, "missing").has_value())
        {
            fail("expected json_get_string missing to be nullopt");
        }
        if (!json_get_string(obj, "s").has_value())
        {
            fail("expected json_get_string present to succeed");
        }
        if (json_get_string(obj, "n").has_value())
        {
            fail("expected json_get_string wrong type to be nullopt");
        }

        if (json_get_number(obj, "missing").has_value())
        {
            fail("expected json_get_number missing to be nullopt");
        }
        if (!json_get_number(obj, "n").has_value())
        {
            fail("expected json_get_number present to succeed");
        }
        if (json_get_number(obj, "s").has_value())
        {
            fail("expected json_get_number wrong type to be nullopt");
        }

        if (!json_get_array(obj, "a").has_value() || json_get_array(obj, "s").has_value())
        {
            fail("expected json_get_array to accept arrays only");
        }
        if (json_get_array(obj, "missing").has_value())
        {
            fail("expected json_get_array missing to be nullopt");
        }
        if (!json_get_object(obj, "o").has_value() || json_get_object(obj, "s").has_value())
        {
            fail("expected json_get_object to accept objects only");
        }
        if (json_get_object(obj, "missing").has_value())
        {
            fail("expected json_get_object missing to be nullopt");
        }
    }

    {
        // lsp_range_json: cover JSON construction for start/end.
        const LspRange r{.start = {.line = 1, .character = 2}, .end = {.line = 3, .character = 4}};
        const auto s = json_serialize(lsp_range_json(r));
        if (s.find("\"start\"") == std::string::npos || s.find("\"end\"") == std::string::npos)
        {
            fail("expected lsp_range_json to include start/end keys");
        }
    }

    {
        // diagnostics_to_json: cover diagnostic span present/absent.
        curlee::source::LineMap map("abc\n");
        std::vector<curlee::diag::Diagnostic> diags;
        diags.push_back(curlee::diag::Diagnostic{.severity = curlee::diag::Severity::Error,
                                                 .message = "no span",
                                                 .span = std::nullopt,
                                                 .notes = {}});
        diags.push_back(curlee::diag::Diagnostic{.severity = curlee::diag::Severity::Warning,
                                                 .message = "with span",
                                                 .span = curlee::source::Span{.start = 0, .end = 1},
                                                 .notes = {}});
        const auto json = diagnostics_to_json(diags, map);
        if (json.find("no span") == std::string::npos ||
            json.find("with span") == std::string::npos)
        {
            fail("expected diagnostics_to_json to include messages");
        }
    }

    {
        // function_signature_to_string and find_function_by_name.
        curlee::parser::Function f;
        f.name = "foo";
        f.params.clear();
        f.body = curlee::parser::Block{.span = {.start = 0, .end = 0}, .stmts = {}};

        if (function_signature_to_string(f) != "fn foo()")
        {
            fail("expected signature without return type");
        }
        f.return_type = curlee::parser::TypeName{.span = {.start = 0, .end = 0}, .name = "Int"};
        if (function_signature_to_string(f).find("-> Int") == std::string::npos)
        {
            fail("expected signature with return type");
        }

        curlee::parser::Program prog;
        prog.functions.push_back(std::move(f));
        if (find_function_by_name(prog, "foo") == nullptr)
        {
            fail("expected find_function_by_name to find function");
        }
        if (find_function_by_name(prog, "bar") != nullptr)
        {
            fail("expected find_function_by_name to miss unknown function");
        }
    }

    {
        // identifier_span_in_definition: drive the different failure modes.
        curlee::resolver::Symbol sym;
        sym.name = "";
        sym.span = curlee::source::Span{.start = 0, .end = 1};
        if (identifier_span_in_definition(sym, "abc").has_value())
        {
            fail("expected identifier_span_in_definition empty name to fail");
        }
        sym.name = "x";
        sym.span = curlee::source::Span{.start = 10, .end = 10};
        if (identifier_span_in_definition(sym, "abc").has_value())
        {
            fail("expected identifier_span_in_definition empty range to fail");
        }
        sym.span = curlee::source::Span{.start = 0, .end = 3};
        if (identifier_span_in_definition(sym, "abc").has_value())
        {
            fail("expected identifier_span_in_definition npos to fail");
        }
        sym.name = "xx";
        sym.span = curlee::source::Span{.start = 1, .end = 2};
        if (identifier_span_in_definition(sym, "xxx").has_value())
        {
            fail("expected identifier_span_in_definition out-of-range find to fail");
        }
        sym.name = "x";
        sym.span = curlee::source::Span{.start = 0, .end = 3};
        const auto ok = identifier_span_in_definition(sym, "x  ");
        if (!ok.has_value())
        {
            fail("expected identifier_span_in_definition to succeed when in-range");
        }
    }

    {
        // read_lsp_message: cover non-CRLF path (no trailing '\r').
        const std::string payload = "{}";
        std::ostringstream oss;
        oss << "Content-Length: " << payload.size() << "\n\n" << payload;
        std::istringstream in(oss.str());
        std::string out;
        if (!read_lsp_message(in, out) || out != payload)
        {
            fail("expected read_lsp_message to parse LF-only headers");
        }
    }

    {
        // collect_diagnostics/analyze basic branches.
        curlee::source::SourceFile file;
        file.path = "file:///tmp/test.curlee";

        file.contents = "@"; // likely lex error
        const auto d_lex = collect_diagnostics(file);
        if (d_lex.empty())
        {
            fail("expected lex error diagnostics");
        }
        if (analyze(file).has_value())
        {
            fail("expected analyze to fail on lex error");
        }

        file.contents = "fn main() -> Int { return x; }"; // resolve error
        const auto d_res = collect_diagnostics(file);
        if (d_res.empty())
        {
            fail("expected resolve error diagnostics");
        }
        if (analyze(file).has_value())
        {
            fail("expected analyze to fail on resolve error");
        }

        file.contents = "fn main() -> Int { return true; }"; // type error
        const auto d_type = collect_diagnostics(file);
        if (d_type.empty())
        {
            fail("expected type error diagnostics");
        }
        if (analyze(file).has_value())
        {
            fail("expected analyze to fail on type error");
        }

        file.contents = "fn main() -> Int { return 0; }";
        const auto d_ok = collect_diagnostics(file);
        if (!d_ok.empty())
        {
            fail("expected no diagnostics for valid program");
        }
        if (!analyze(file).has_value())
        {
            fail("expected analyze to succeed on valid program");
        }
    }

    {
        // Drive the LSP main loop in-process to hit request-handler guard branches.
        const std::string init = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                                 "\"params\":{\"capabilities\":{}}}";
        const std::string init_no_id =
            "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"params\":{\"capabilities\":{}}}";

        const std::string uri = "file:///tmp/ok.curlee";

        // didOpen without params -> should be ignored.
        const std::string did_open_no_params =
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\"}";

        // didOpen with params but missing uri.
        const std::string did_open_missing_uri = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                                                 "didOpen\",\"params\":{\"textDocument\":{\"text\":"
                                                 "\"fn main() -> Int { return 0; }\"}}}";

        // didOpen with missing textDocument.
        const std::string did_open_missing_text_document =
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{}}";

        // didOpen success (stores document and publishes diagnostics).
        const std::string doc_open = "fn foo(x: Int) -> Int { return 0; } fn bar() -> Int { return "
                                     "0; } fn main() -> Int { return foo(0); }";
        const std::string did_open_ok =
            std::string("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                        "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\",\"text\":\"" + json_escape(doc_open) + "\"}}}";

        // didChange with missing text in change entry.
        const std::string did_change_missing_text =
            "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
            "didChange\",\"params\":{\"textDocument\":{\"uri\":\"file:///tmp/"
            "x.curlee\",\"version\":2},\"contentChanges\":[{\"range\":null}]}}";

        // didChange with empty contentChanges.
        const std::string did_change_empty_changes =
            std::string("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                        "didChange\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\",\"version\":2},\"contentChanges\":[]}}";

        // didChange with non-object first change.
        const std::string did_change_first_not_object =
            std::string("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                        "didChange\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\",\"version\":2},\"contentChanges\":[0]}}";

        // didChange success (updates document to a version with contracts/refinements).
        const std::string doc_change =
            "enum E { V; } "
            "fn foo(x: Int where true) -> Int [ requires true; requires true; ] { return 0; } "
            "fn bar() -> Int { return 0; } "
            "fn main() -> Int { let e: E = E::V(); let z: Int = foo(0); unsafe { "
            "python_ffi.call(); } return bar(); }";
        const std::string did_change_ok =
            std::string("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                        "didChange\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\",\"version\":3},\"contentChanges\":[{\"text\":\"" + json_escape(doc_change) +
            "\"}]}}";

        // Sanity-check that this document actually type-checks; otherwise hover/definition
        // paths will be skipped and branch coverage will stall.
        {
            const curlee::source::SourceFile file{.path = "/tmp/ok.curlee", .contents = doc_change};

            const auto lexed = curlee::lexer::lex(file.contents);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                const auto& d = std::get<curlee::diag::Diagnostic>(lexed);
                fail(std::string("doc_change lex failed: ") + d.message);
            }

            auto parsed = curlee::parser::parse(std::get<std::vector<curlee::lexer::Token>>(lexed));
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                const auto& ds = std::get<std::vector<curlee::diag::Diagnostic>>(parsed);
                if (ds.empty())
                {
                    fail("doc_change parse failed with empty diagnostics");
                }
                fail(std::string("doc_change parse failed: ") + ds.front().message);
            }

            auto program = std::get<curlee::parser::Program>(std::move(parsed));
            const auto resolved = curlee::resolver::resolve(program, file);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(resolved))
            {
                const auto& ds = std::get<std::vector<curlee::diag::Diagnostic>>(resolved);
                if (ds.empty())
                {
                    fail("doc_change resolve failed with empty diagnostics");
                }
                fail(std::string("doc_change resolve failed: ") + ds.front().message);
            }

            const auto typed = curlee::types::type_check(program);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
            {
                const auto& ds = std::get<std::vector<curlee::diag::Diagnostic>>(typed);
                if (ds.empty())
                {
                    fail("doc_change type_check failed with empty diagnostics");
                }
                fail(std::string("doc_change type_check failed: ") + ds.front().message);
            }

            const auto a = analyze(file);
            if (!a.has_value())
            {
                fail("expected analyze() to succeed for doc_change (unexpected) ");
            }
        }

        // hover missing params.
        const std::string hover_missing_params =
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/hover\"}";

        // hover with params but missing textDocument/position.
        const std::string hover_missing_position =
            std::string("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"textDocument/"
                        "hover\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\"}}}";

        // hover with params but missing uri.
        const std::string hover_missing_uri =
            "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"textDocument/"
            "hover\",\"params\":{\"textDocument\":{},\"position\":{\"line\":0,\"character\":0}}}";

        // hover with params but missing character.
        const std::string hover_missing_character =
            std::string("{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"textDocument/"
                        "hover\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\"},\"position\":{\"line\":0}}}";

        // hover with position but missing textDocument (exercises short-circuit branch).
        const std::string hover_missing_text_document =
            "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"textDocument/"
            "hover\",\"params\":{\"position\":{\"line\":0,\"character\":0}}}";

        // Compute stable positions in the changed document (single-line).
        const std::size_t foo_call = doc_change.find("foo(0)");
        const std::size_t bar_call = doc_change.rfind("bar()");
        const std::size_t scoped_call = doc_change.find("E::V()");
        const std::size_t literal_0 = doc_change.find("return 0;");
        if (foo_call == std::string::npos || bar_call == std::string::npos ||
            scoped_call == std::string::npos || literal_0 == std::string::npos)
        {
            fail("expected to find call/literal substrings in doc_change");
        }

        // Find a stable offset within `E::V()` that:
        // - selects the `E::V()` call as `best_call` (callee is not a NameExpr, so the hover
        // handler
        //   takes the `callee_name == nullptr` branch), and
        // - selects a `best_expr` whose id is *not* present in `TypeInfo::expr_types`.
        //
        // This works because the type checker does not currently record types for call *callee*
        // expressions (only for the call expression itself + arguments).
        std::size_t scoped_callee_untyped_offset = scoped_call + 1;
        {
            curlee::source::SourceFile file;
            file.path = "/tmp/ok.curlee";
            file.contents = doc_change;
            const auto a = analyze(file);
            if (!a.has_value())
            {
                fail("expected analyze() to succeed for doc_change when computing offsets");
            }

            const auto best_expr_at = [&](std::size_t off) -> const curlee::parser::Expr*
            {
                const curlee::parser::Expr* best_expr = nullptr;
                for (const auto& func : a->program.functions)
                {
                    for (const auto& stmt : func.body.stmts)
                    {
                        find_exprs_in_stmt(stmt, off, best_expr);
                    }
                }
                return best_expr;
            };

            const auto best_call_at = [&](std::size_t off) -> const curlee::parser::Expr*
            {
                const curlee::parser::Expr* best_call = nullptr;
                for (const auto& func : a->program.functions)
                {
                    for (const auto& stmt : func.body.stmts)
                    {
                        find_call_exprs_in_stmt(stmt, off, best_call);
                    }
                }
                return best_call;
            };

            bool found = false;
            const std::size_t limit = std::min(doc_change.size(), scoped_call + 16);
            for (std::size_t off = scoped_call; off < limit; ++off)
            {
                const auto* call_e = best_call_at(off);
                if (call_e == nullptr)
                {
                    continue;
                }
                const auto* call = std::get_if<curlee::parser::CallExpr>(&call_e->node);
                if (call == nullptr || call->callee == nullptr)
                {
                    continue;
                }
                if (std::get_if<curlee::parser::ScopedNameExpr>(&call->callee->node) == nullptr)
                {
                    continue;
                }

                const auto* e = best_expr_at(off);
                if (e == nullptr)
                {
                    continue;
                }
                if (a->type_info.expr_types.find(e->id) == a->type_info.expr_types.end())
                {
                    scoped_callee_untyped_offset = off;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                fail("expected to find an untyped callee subexpression offset within E::V()");
            }
        }

        // hover: no id (tests response id guard), over foo(0) to produce obligations.
        const std::string hover_no_id =
            std::string("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/"
                        "hover\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\"},\"position\":{\"line\":0,\"character\":" + std::to_string(foo_call) + "}}}";

        // hover with id over bar() to produce empty obligations.
        const std::string hover_bar =
            std::string("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"textDocument/"
                        "hover\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\"},\"position\":{\"line\":0,\"character\":" + std::to_string(bar_call) + "}}}";

        // hover over E::V() (scoped-name callee) to hit callee_name == nullptr and the
        // `expr_types.find(...) == end` hover branch.
        const std::string hover_member_callee =
            std::string("{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"textDocument/"
                        "hover\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\"},\"position\":{\"line\":0,\"character\":" +
            std::to_string(scoped_callee_untyped_offset) + "}}}";

        // hover over a non-call expression (the literal in return 0) to exercise type hover.
        const std::string hover_literal =
            std::string("{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"textDocument/"
                        "hover\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\"},\"position\":{\"line\":0,\"character\":" + std::to_string(literal_0 + 7) +
            "}}}";

        // definition on a use (should return a location) and a position with no use.
        const std::string definition_on_use =
            std::string("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"textDocument/"
                        "definition\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\"},\"position\":{\"line\":0,\"character\":" + std::to_string(foo_call) + "}}}";

        // definition on bar() so the symbol scan sees at least one non-match first.
        const std::string definition_on_bar =
            std::string("{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"textDocument/"
                        "definition\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\"},\"position\":{\"line\":0,\"character\":" + std::to_string(bar_call) + "}}}";

        // definition with no id (covers response id-guard false branch).
        const std::string definition_no_id =
            std::string("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/definition\","
                        "\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\"},\"position\":{\"line\":0,\"character\":" + std::to_string(foo_call) + "}}}";
        const std::string definition_no_use =
            std::string("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"textDocument/"
                        "definition\",\"params\":{\"textDocument\":{\"uri\":\"") +
            uri + "\"},\"position\":{\"line\":0,\"character\":0}}}";

        const std::string shutdown =
            "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"shutdown\",\"params\":{}}";
        const std::string exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";

        std::string in_data;
        in_data += lsp_frame(init);
        in_data += lsp_frame(init_no_id);
        in_data += lsp_frame(did_open_no_params);
        in_data += lsp_frame(did_open_missing_uri);
        in_data += lsp_frame(did_open_missing_text_document);
        in_data += lsp_frame(did_open_ok);
        in_data += lsp_frame(did_change_missing_text);
        in_data += lsp_frame(did_change_empty_changes);
        in_data += lsp_frame(did_change_first_not_object);
        in_data += lsp_frame(did_change_ok);
        in_data += lsp_frame(hover_missing_params);
        in_data += lsp_frame(hover_missing_position);
        in_data += lsp_frame(hover_missing_uri);
        in_data += lsp_frame(hover_missing_character);
        in_data += lsp_frame(hover_missing_text_document);
        in_data += lsp_frame(hover_no_id);
        in_data += lsp_frame(hover_bar);
        in_data += lsp_frame(hover_member_callee);
        in_data += lsp_frame(hover_literal);
        in_data += lsp_frame(definition_on_use);
        in_data += lsp_frame(definition_on_bar);
        in_data += lsp_frame(definition_no_id);
        in_data += lsp_frame(definition_no_use);
        in_data += lsp_frame(shutdown);
        in_data += lsp_frame(exit);

        std::istringstream in(in_data);
        std::ostringstream out;

        auto* old_in = std::cin.rdbuf(in.rdbuf());
        auto* old_out = std::cout.rdbuf(out.rdbuf());

        (void)curlee_lsp_main();

        std::cin.rdbuf(old_in);
        std::cout.rdbuf(old_out);

        const std::string out_str = out.str();
        if (out_str.find("\"id\":1") == std::string::npos)
        {
            fail("expected initialize response in output");
        }
        if (out_str.find("textDocument/publishDiagnostics") == std::string::npos)
        {
            fail("expected publishDiagnostics notification");
        }

        // We send two valid document updates (didOpen + didChange), so we should observe
        // at least two publishDiagnostics notifications.
        {
            std::size_t count = 0;
            std::size_t pos = 0;
            while ((pos = out_str.find("textDocument/publishDiagnostics", pos)) !=
                   std::string::npos)
            {
                ++count;
                ++pos;
            }
            if (count < 2)
            {
                fail("expected >=2 publishDiagnostics notifications");
            }
        }

        // Ensure we actually produced hover/definition responses with ids.
        if (out_str.find("\"id\":5") == std::string::npos)
        {
            fail("expected definition response for id=5");
        }
        if (out_str.find("\"id\":14") == std::string::npos)
        {
            fail("expected definition response for id=14");
        }
        if (out_str.find("\"id\":9") == std::string::npos)
        {
            fail("expected hover response for id=9");
        }
        if (out_str.find("\"id\":10") == std::string::npos)
        {
            fail("expected hover response for id=10");
        }
        if (out_str.find("Content-Length:") == std::string::npos)
        {
            fail("expected framed LSP output");
        }

        // Parse the framed output and ensure specific responses took the intended success paths.
        {
            std::istringstream framed(out_str);
            std::string payload;

            bool saw_definition_5 = false;
            bool saw_definition_14 = false;
            bool saw_hover_9_with_range = false;
            bool saw_hover_10_null = false;
            bool saw_hover_12_type = false;

            while (read_lsp_message(framed, payload))
            {
                const auto parsed = parse_json(payload);
                if (!parsed.has_value() || !parsed->is_object())
                {
                    continue;
                }

                const auto& root = *parsed->as_object();
                const auto id = json_get_number(root, "id");
                if (!id.has_value())
                {
                    continue;
                }

                if (*id == 5)
                {
                    const auto result = json_get_object(root, "result");
                    if (!result.has_value())
                    {
                        fail("expected definition id=5 to return an object result");
                    }
                    const auto uri2 = json_get_string(*result->as_object(), "uri");
                    const auto range = json_get_object(*result->as_object(), "range");
                    if (!uri2.has_value() || !range.has_value())
                    {
                        fail("expected definition id=5 to include uri+range");
                    }
                    saw_definition_5 = true;
                }
                else if (*id == 14)
                {
                    const auto result = json_get_object(root, "result");
                    if (!result.has_value())
                    {
                        fail("expected definition id=14 to return an object result");
                    }
                    const auto uri2 = json_get_string(*result->as_object(), "uri");
                    const auto range = json_get_object(*result->as_object(), "range");
                    if (!uri2.has_value() || !range.has_value())
                    {
                        fail("expected definition id=14 to include uri+range");
                    }
                    saw_definition_14 = true;
                }
                else if (*id == 9)
                {
                    const auto result = json_get_object(root, "result");
                    if (!result.has_value())
                    {
                        fail("expected hover id=9 to return an object result");
                    }
                    const auto range = json_get_object(*result->as_object(), "range");
                    if (!range.has_value())
                    {
                        fail("expected hover id=9 to include a range (call hover path)");
                    }
                    saw_hover_9_with_range = true;
                }
                else if (*id == 12)
                {
                    const auto result = json_get_object(root, "result");
                    if (!result.has_value())
                    {
                        fail("expected hover id=12 to return an object result");
                    }
                    const auto contents = json_get_object(*result->as_object(), "contents");
                    if (!contents.has_value())
                    {
                        fail("expected hover id=12 to include contents (type hover path)");
                    }
                    saw_hover_12_type = true;
                }
                else if (*id == 10)
                {
                    const auto it = root.find("result");
                    if (it == root.end())
                    {
                        fail("expected hover id=10 to include result");
                    }
                    if (!it->second.is_null())
                    {
                        fail("expected hover id=10 to return null result (member callee + untyped "
                             "expr)");
                    }
                    saw_hover_10_null = true;
                }
            }

            if (!saw_definition_5)
            {
                fail("expected to observe definition response for id=5");
            }
            if (!saw_definition_14)
            {
                fail("expected to observe definition response for id=14");
            }
            if (!saw_hover_9_with_range)
            {
                fail("expected to observe hover response for id=9 with range");
            }
            if (!saw_hover_10_null)
            {
                fail("expected to observe hover response for id=10 returning null");
            }
            if (!saw_hover_12_type)
            {
                fail("expected to observe type hover response for id=12");
            }
        }
    }

    {
        // Exercise lex-error branch in collect_diagnostics.
        const curlee::source::SourceFile bad{.path = "/tmp/bad.curlee", .contents = "@"};
        const auto diags = collect_diagnostics(bad);
        if (diags.empty())
        {
            fail("expected diagnostics for invalid character");
        }
    }

    {
        // Ensure analyze() failure at the type-check stage is covered.
        const std::string bad = "fn main() -> Int { return true; }";
        const curlee::source::SourceFile file{.path = "/tmp/bad_type.curlee", .contents = bad};
        const auto a = analyze(file);
        if (a.has_value())
        {
            fail("expected analyze() to fail for type error");
        }
    }

    {
        // Manually build partial ASTs to cover null-pointer branches in find_* helpers.
        using curlee::parser::Expr;
        using curlee::parser::Stmt;

        const auto span = curlee::source::Span{.start = 0, .end = 10};
        const auto make_leaf = [&](std::size_t id) -> Expr
        { return Expr{.id = id, .span = span, .node = curlee::parser::IntExpr{.lexeme = "0"}}; };

        Expr unary_null{.id = 2,
                        .span = span,
                        .node = curlee::parser::UnaryExpr{.op = curlee::lexer::TokenKind::Bang,
                                                          .rhs = nullptr}};
        (void)find_expr_at(unary_null, 1, nullptr);
        (void)find_call_expr_at(unary_null, 1, nullptr);

        Expr binary_null{.id = 3,
                         .span = span,
                         .node = curlee::parser::BinaryExpr{
                             .op = curlee::lexer::TokenKind::Plus, .lhs = nullptr, .rhs = nullptr}};
        (void)find_expr_at(binary_null, 1, nullptr);
        (void)find_call_expr_at(binary_null, 1, nullptr);

        Expr call_null_callee{
            .id = 4, .span = span, .node = curlee::parser::CallExpr{.callee = nullptr, .args = {}}};
        (void)find_expr_at(call_null_callee, 1, nullptr);
        (void)find_call_expr_at(call_null_callee, 1, nullptr);

        // Cover the false branch of `if (!best || expr.span.length() < best->span.length())`
        // inside find_call_expr_at.
        Expr small_call{.id = 40,
                        .span = curlee::source::Span{.start = 0, .end = 1},
                        .node = curlee::parser::CallExpr{.callee = nullptr, .args = {}}};
        Expr big_call{.id = 41,
                      .span = curlee::source::Span{.start = 0, .end = 10},
                      .node = curlee::parser::CallExpr{.callee = nullptr, .args = {}}};
        (void)find_call_expr_at(big_call, 0, &small_call);
        (void)find_call_expr_at(small_call, 0, &big_call);

        Expr member_null{.id = 5,
                         .span = span,
                         .node = curlee::parser::MemberExpr{.base = nullptr, .member = "m"}};
        (void)find_expr_at(member_null, 1, nullptr);
        (void)find_call_expr_at(member_null, 1, nullptr);

        Expr group_null{.id = 6, .span = span, .node = curlee::parser::GroupExpr{.inner = nullptr}};
        (void)find_expr_at(group_null, 1, nullptr);
        (void)find_call_expr_at(group_null, 1, nullptr);

        // Also hit the initial predicate in find_call_expr_at for non-call nodes and
        // out-of-span offsets.
        const Expr leaf = make_leaf(30);
        (void)find_call_expr_at(leaf, 1, nullptr);
        (void)find_call_expr_at(call_null_callee, 999, nullptr);

        Stmt ret_no_value{.span = span, .node = curlee::parser::ReturnStmt{.value = std::nullopt}};
        Stmt ret_with_value{
            .span = span,
            .node =
                curlee::parser::ReturnStmt{.value =
                                               std::optional<curlee::parser::Expr>{make_leaf(10)}},
        };

        Stmt if_null_blocks{.span = span,
                            .node = curlee::parser::IfStmt{.cond = make_leaf(11),
                                                           .then_block = nullptr,
                                                           .else_block = nullptr}};
        Stmt while_null_body{.span = span,
                             .node =
                                 curlee::parser::WhileStmt{.cond = make_leaf(12), .body = nullptr}};
        Stmt block_null{.span = span, .node = curlee::parser::BlockStmt{.block = nullptr}};
        Stmt unsafe_null{.span = span, .node = curlee::parser::UnsafeStmt{.body = nullptr}};

        // Also cover the `unsafe_stmt->body` true branches (empty and non-empty bodies).
        curlee::parser::Block empty_block{.span = span, .stmts = {}};
        Stmt unsafe_empty{
            .span = span,
            .node = curlee::parser::UnsafeStmt{
                .body = std::make_unique<curlee::parser::Block>(std::move(empty_block))}};

        Stmt inner_ret{.span = span, .node = curlee::parser::ReturnStmt{.value = std::nullopt}};
        curlee::parser::Block one_block{.span = span, .stmts = {}};
        one_block.stmts.push_back(std::move(inner_ret));
        Stmt unsafe_one{.span = span,
                        .node = curlee::parser::UnsafeStmt{
                            .body = std::make_unique<curlee::parser::Block>(std::move(one_block))}};

        const curlee::parser::Expr* best = nullptr;
        find_call_exprs_in_stmt(ret_no_value, 1, best);
        find_call_exprs_in_stmt(ret_with_value, 1, best);
        find_call_exprs_in_stmt(if_null_blocks, 1, best);
        find_call_exprs_in_stmt(while_null_body, 1, best);
        find_call_exprs_in_stmt(block_null, 1, best);
        find_call_exprs_in_stmt(unsafe_null, 1, best);
        find_call_exprs_in_stmt(unsafe_empty, 1, best);
        find_call_exprs_in_stmt(unsafe_one, 1, best);

        best = nullptr;
        find_exprs_in_stmt(ret_no_value, 1, best);
        find_exprs_in_stmt(ret_with_value, 1, best);
        find_exprs_in_stmt(if_null_blocks, 1, best);
        find_exprs_in_stmt(while_null_body, 1, best);
        find_exprs_in_stmt(block_null, 1, best);
        find_exprs_in_stmt(unsafe_null, 1, best);
        find_exprs_in_stmt(unsafe_empty, 1, best);
        find_exprs_in_stmt(unsafe_one, 1, best);
    }

    std::cout << "OK\n";
    return 0;
}
