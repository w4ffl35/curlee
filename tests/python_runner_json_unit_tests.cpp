#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

// This is a white-box unit test for the JSON parser/serializer in
// src/interop/python_runner_main.cpp.
//
// Rationale: the python runner is a standalone binary, but its internal JSON
// helpers have several error/edge branches that are hard to reach through the
// stable request/response protocol alone. For coverage and robustness we compile
// the implementation into this test TU and exercise those helpers directly.
#define main curlee_python_runner_main__do_not_call
#include "../src/interop/python_runner_main.cpp"
#undef main

struct MainResult
{
    int exit_code = -1;
    std::string out;
};

static MainResult run_runner_main_with_io(const std::string& stdin_text)
{
    std::istringstream in(stdin_text);
    std::ostringstream out;

    auto* old_in = std::cin.rdbuf(in.rdbuf());
    auto* old_out = std::cout.rdbuf(out.rdbuf());
    const int rc = curlee_python_runner_main__do_not_call();
    std::cin.rdbuf(old_in);
    std::cout.rdbuf(old_out);

    return MainResult{.exit_code = rc, .out = out.str()};
}

int main()
{
    // parse_json: empty/whitespace-only input should fail inside parse_value() after skip_ws().
    if (parse_json("") != std::nullopt)
    {
        fail("expected empty string to be malformed json");
    }
    if (parse_json("   \t\r\n") != std::nullopt)
    {
        fail("expected whitespace-only to be malformed json");
    }

    // Directly exercise consume() failure branch via parse_string() on a non-string input.
    {
        JsonParser p{"123"};
        if (p.parse_string().has_value())
        {
            fail("expected parse_string to fail when not starting with quote");
        }
    }

    // parse_string: backslash at EOF should fail.
    {
        std::string s;
        s.push_back('"');
        s.push_back('\\');
        if (parse_json(s) != std::nullopt)
        {
            fail("expected unterminated escape to be malformed json");
        }
    }

    // parse_value: unknown leading character should fail.
    if (parse_json("@") != std::nullopt)
    {
        fail("expected '@' to be malformed json");
    }

    // parse_value: invalid keyword prefixes should fail.
    if (parse_json("nul") != std::nullopt)
    {
        fail("expected 'nul' to be malformed json");
    }
    if (parse_json("tru") != std::nullopt)
    {
        fail("expected 'tru' to be malformed json");
    }
    if (parse_json("fals") != std::nullopt)
    {
        fail("expected 'fals' to be malformed json");
    }

    // parse_number: '-' alone should fail (strtod consumes nothing).
    if (parse_json("-") != std::nullopt)
    {
        fail("expected '-' to be malformed json number");
    }

    // parse_number: call parse_number() directly at EOF to exercise the
    // has_leading_minus = !eof() && ... short-circuit branch when eof() is true.
    {
        JsonParser p{""};
        const auto v = p.parse_number();
        if (v.has_value())
        {
            fail("expected parse_number to fail on empty input");
        }
    }

    // parse_number: exercise sign, decimal, exponent branches via direct parse_number().
    {
        JsonParser p{"-12.34e+5"};
        const auto v = p.parse_number();
        if (!v.has_value() || !v->is_number())
        {
            fail("expected parse_number to parse -12.34e+5");
        }
    }
    {
        JsonParser p{"0.5"};
        const auto v = p.parse_number();
        if (!v.has_value() || !v->is_number())
        {
            fail("expected parse_number to parse 0.5");
        }
    }
    {
        JsonParser p{"1E-2"};
        const auto v = p.parse_number();
        if (!v.has_value() || !v->is_number())
        {
            fail("expected parse_number to parse 1E-2");
        }
    }

    // parse_number: force the has_exp_sign = !eof() && (...) branch where eof() is true
    // immediately after consuming 'e'. This is not reachable via strict JSON input, so we
    // mutate parser state directly (white-box test).
    {
        JsonParser p{"0e"};
        p.pos = 1; // point at 'e' so parse_number() enters exponent-handling with eof() after ++pos
        const auto v = p.parse_number();
        if (v.has_value())
        {
            fail("expected parse_number to fail when starting at an exponent marker");
        }
    }

    // parse_string: invalid escape should fail.
    if (parse_json("\"\\u1234\"") != std::nullopt)
    {
        fail("expected \\u escape to be unsupported and fail");
    }

    // parse_array/object: structural errors.
    if (parse_json("[1 2]") != std::nullopt)
    {
        fail("expected missing comma in array to be malformed");
    }
    // consume('[') failure: call parse_array() when not at '['.
    {
        JsonParser p{"0"};
        if (p.parse_array().has_value())
        {
            fail("expected parse_array to fail when not starting with '['");
        }
    }
    if (parse_json("{\"k\" 1}") != std::nullopt)
    {
        fail("expected missing ':' in object to be malformed");
    }
    // consume('{') failure: call parse_object() when not at '{'.
    {
        JsonParser p{"[]"};
        if (p.parse_object().has_value())
        {
            fail("expected parse_object to fail when not starting with '{'");
        }
    }
    if (parse_json("{1:2}") != std::nullopt)
    {
        fail("expected non-string object key to be malformed");
    }

    // parse_array: value parse failure inside the loop.
    {
        JsonParser p{"[,]"};
        if (p.parse_array().has_value())
        {
            fail("expected '[,]' to be malformed array");
        }
    }

    // parse_object: key parse failure inside the loop.
    {
        JsonParser p{"{1:2}"};
        if (p.parse_object().has_value())
        {
            fail("expected '{1:2}' to be malformed object");
        }
    }

    // parse_object: missing comma between fields.
    if (parse_json("{\"a\":1 \"b\":2}") != std::nullopt)
    {
        fail("expected object missing comma to be malformed");
    }

    // parse_json: trailing garbage should be rejected.
    if (parse_json("{} trailing") != std::nullopt)
    {
        fail("expected trailing garbage to be rejected");
    }

    // json_escape/json_serialize: cover all scalar kinds plus arrays/objects.
    {
        Json::Array arr;
        arr.push_back(Json{std::nullptr_t{}});
        arr.push_back(Json{true});
        arr.push_back(Json{false});
        arr.push_back(Json{1.25});
        arr.push_back(Json{std::string("a\"b\\c\n\r\t")});

        Json::Object inner;
        inner.emplace("x", Json{std::string("y")});
        arr.push_back(Json{inner});

        const Json top{arr};
        const std::string s = json_serialize(top);
        if (s.find("\\\"") == std::string::npos || s.find("\\\\") == std::string::npos ||
            s.find("\\n") == std::string::npos || s.find("\\r") == std::string::npos ||
            s.find("\\t") == std::string::npos)
        {
            fail("expected json_serialize to escape special characters");
        }
        if (s.front() != '[' || s.back() != ']')
        {
            fail("expected array serialization to produce [...] ");
        }
    }

    // json_get_* helpers.
    {
        Json::Object obj;
        obj.emplace("s", Json{std::string("hi")});
        obj.emplace("n", Json{2.0});
        obj.emplace("o", Json{Json::Object{{"k", Json{std::string("v")}}}});

        const auto s = json_get_string(obj, "s");
        if (!s.has_value() || *s != "hi")
        {
            fail("json_get_string failed");
        }
        const auto n = json_get_number(obj, "n");
        if (!n.has_value() || *n != 2.0)
        {
            fail("json_get_number failed");
        }
        const auto o = json_get_object(obj, "o");
        if (!o.has_value() || !o->is_object())
        {
            fail("json_get_object failed");
        }

        if (json_get_string(obj, "missing").has_value())
        {
            fail("json_get_string should return nullopt for missing key");
        }
        if (json_get_number(obj, "s").has_value())
        {
            fail("json_get_number should return nullopt for wrong type");
        }
    }

    // Direct call: is_integral should be exercised for finite/NaN.
    {
        if (!is_integral(1.0) || is_integral(std::numeric_limits<double>::quiet_NaN()))
        {
            fail("unexpected is_integral behavior");
        }
    }

    // Run the (renamed) runner main() in-process to cover request-handling branches.
    {
        // Empty input.
        const auto r0 = run_runner_main_with_io("");
        if (r0.exit_code != 2 || r0.out.find("\"message\":\"empty input\"") == std::string::npos)
        {
            fail("expected empty input handling in main()");
        }

        // Malformed JSON.
        const auto r1 = run_runner_main_with_io("{\"protocol_version\":1,\n");
        if (r1.exit_code != 2 || r1.out.find("\"message\":\"malformed json\"") == std::string::npos)
        {
            fail("expected malformed json handling in main()");
        }

        // Unsupported protocol_version.
        const auto r2 =
            run_runner_main_with_io("{\"protocol_version\":2,\"id\":\"p\",\"op\":\"handshake\"}\n");
        if (r2.exit_code != 2 ||
            r2.out.find("\"kind\":\"protocol_version_unsupported\"") == std::string::npos)
        {
            fail("expected unsupported protocol_version handling in main()");
        }

        // Missing op.
        const auto r3 = run_runner_main_with_io("{\"protocol_version\":1,\"id\":\"m\"}\n");
        if (r3.exit_code != 2 || r3.out.find("\"message\":\"missing op\"") == std::string::npos)
        {
            fail("expected missing op handling in main()");
        }

        // Unknown op.
        const auto r4 =
            run_runner_main_with_io("{\"protocol_version\":1,\"id\":\"u\",\"op\":\"nope\"}\n");
        if (r4.exit_code != 2 || r4.out.find("\"message\":\"unknown op\"") == std::string::npos)
        {
            fail("expected unknown op handling in main()");
        }

        // Echo missing payload.
        const auto r5 =
            run_runner_main_with_io("{\"protocol_version\":1,\"id\":\"e1\",\"op\":\"echo\"}\n");
        if (r5.exit_code != 2 ||
            r5.out.find("\"message\":\"missing echo payload\"") == std::string::npos)
        {
            fail("expected missing echo payload handling in main()");
        }

        // Echo wrong payload type.
        const auto r6 = run_runner_main_with_io(
            "{\"protocol_version\":1,\"id\":\"e2\",\"op\":\"echo\",\"echo\":{\"value\":1}}\n");
        if (r6.exit_code != 2 ||
            r6.out.find("\"message\":\"echo.value must be string\"") == std::string::npos)
        {
            fail("expected echo.value type error handling in main()");
        }

        // Echo success with special characters to cover json_escape in the normal response path.
        const auto r7 =
            run_runner_main_with_io("{\"protocol_version\":1,\"id\":\"e3\",\"op\":\"echo\","
                                    "\"echo\":{\"value\":\"a\\n\\t\\r\\\\\\\"b\"}}\n");
        if (r7.exit_code != 0)
        {
            fail("expected echo with escapes to succeed");
        }
        if (r7.out.find("\\n") == std::string::npos || r7.out.find("\\t") == std::string::npos ||
            r7.out.find("\\r") == std::string::npos || r7.out.find("\\\\") == std::string::npos ||
            r7.out.find("\\\"") == std::string::npos)
        {
            fail("expected echoed string to be json-escaped in output");
        }

        // Handshake success with id present to cover id extraction branch.
        const auto r8 =
            run_runner_main_with_io("{\"protocol_version\":1,\"id\":\"h\",\"op\":\"handshake\"}\n");
        if (r8.exit_code != 0 || r8.out.find("\"id\":\"h\"") == std::string::npos)
        {
            fail("expected handshake with id to succeed and echo id");
        }
    }

    std::cout << "OK\n";
    return 0;
}
