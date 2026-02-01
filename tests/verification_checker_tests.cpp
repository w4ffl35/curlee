#include <cstdlib>
#include <curlee/diag/diagnostic.h>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/types/type_check.h>
#include <curlee/verification/checker.h>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static curlee::parser::Program parse_program_or_fail(std::string_view source,
                                                     const std::string& what)
{
    const auto lexed = curlee::lexer::lex(source);
    if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
    {
        fail("expected lexing to succeed for " + what);
    }

    const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
    auto parsed = curlee::parser::parse(tokens);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
    {
        fail("expected parsing to succeed for " + what);
    }

    return std::get<curlee::parser::Program>(std::move(parsed));
}

static std::variant<curlee::verification::Verified, std::vector<curlee::diag::Diagnostic>>
verify_program(std::string_view source, const std::string& what)
{
    auto program = parse_program_or_fail(source, what);

    const auto typed = curlee::types::type_check(program);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
    {
        fail("expected type checking to succeed for " + what);
    }

    const auto& type_info = std::get<curlee::types::TypeInfo>(typed);
    return curlee::verification::verify(program, type_info);
}

static bool has_message_substr(const std::vector<curlee::diag::Diagnostic>& diags,
                               std::string_view needle)
{
    for (const auto& d : diags)
    {
        if (d.message.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

static bool any_note_has_prefix(const std::vector<curlee::diag::Diagnostic>& diags,
                                std::string_view prefix)
{
    for (const auto& d : diags)
    {
        for (const auto& n : d.notes)
        {
            if (n.message.rfind(prefix, 0) == 0)
            {
                return true;
            }
        }
    }
    return false;
}

int main()
{
    {
        // Call-site checking: requires clause should be enforced on calls.
        const std::string source = "fn take_nonzero(x: Int where x > 0) -> Int [\n"
                                   "  requires x != 0;\n"
                                   "] {\n"
                                   "  return x;\n"
                                   "}\n"
                                   "fn main() -> Int {\n"
                                   "  return take_nonzero(0);\n"
                                   "}\n";

        const auto verified = verify_program(source, "requires clause call-site test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for requires clause call-site test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "requires clause not satisfied"))
        {
            fail("expected requires clause not satisfied diagnostic");
        }
        if (!any_note_has_prefix(diags, "goal: "))
        {
            fail("expected goal note for requires failure");
        }
        if (!any_note_has_prefix(diags, "model:"))
        {
            fail("expected model note for requires failure");
        }
        if (!any_note_has_prefix(diags, "hint:"))
        {
            fail("expected hint note for requires failure");
        }
    }

    {
        // Return checking: ensures clause should be enforced.
        const std::string source = "fn pos() -> Int [\n"
                                   "  ensures result > 0;\n"
                                   "] {\n"
                                   "  return 0;\n"
                                   "}\n"
                                   "fn main() -> Int {\n"
                                   "  return pos();\n"
                                   "}\n";

        const auto verified = verify_program(source, "ensures clause return test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for ensures clause return test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "ensures clause not satisfied"))
        {
            fail("expected ensures clause not satisfied diagnostic");
        }
        if (!any_note_has_prefix(diags, "goal: "))
        {
            fail("expected goal note for ensures failure");
        }
        if (!any_note_has_prefix(diags, "model:"))
        {
            fail("expected model note for ensures failure");
        }
        if (!any_note_has_prefix(diags, "hint:"))
        {
            fail("expected hint note for ensures failure");
        }
    }

    {
        // Expressions in verification: non-linear multiplication is rejected.
        const std::string source = "fn bad_mul(a: Int, b: Int) -> Int [\n"
                                   "  ensures result > 0;\n"
                                   "] {\n"
                                   "  return a * b;\n"
                                   "}\n"
                                   "fn main() -> Int {\n"
                                   "  return bad_mul(2, 3);\n"
                                   "}\n";

        const auto verified = verify_program(source, "non-linear multiplication test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for non-linear multiplication test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "non-linear multiplication is not supported"))
        {
            fail("expected non-linear multiplication diagnostic");
        }
    }

    {
        // Expressions in verification: calls are not supported in expressions lowered to the
        // solver.
        const std::string source = "fn id(x: Int) -> Int { return x; }\n"
                                   "fn main() -> Int [ ensures result > 0; ] {\n"
                                   "  return id(1);\n"
                                   "}\n";

        const auto verified = verify_program(source, "calls unsupported in verification expr test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for call-in-expression test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "calls are not supported in verification expressions"))
        {
            fail("expected calls unsupported diagnostic");
        }
    }

    {
        // Signature filtering: verification only supports Int/Bool.
        const std::string source = "fn bad_param(x: String) -> Int { return 0; }\n"
                                   "fn main() -> Int { return 0; }\n";

        const auto verified = verify_program(source, "unsupported signature type test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for unsupported signature type test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "verification does not support type"))
        {
            fail("expected unsupported type diagnostic for signature");
        }
    }

    {
        // Let refinements on non-scalar types are rejected.
        const std::string source = "struct S { x: Int; }\n"
                                   "fn main() -> Int {\n"
                                   "  let s: S where s > 0 = S{ x: 1 };\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "non-scalar refinement test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for non-scalar refinement test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "verification does not support refinements on non-scalar"))
        {
            fail("expected non-scalar refinement diagnostic");
        }
    }

    {
        // Let bindings of unsupported scalar types are rejected.
        const std::string source = "fn main() -> Int {\n"
                                   "  let s: String = \"hi\";\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "unsupported let type test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for unsupported let type test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "verification does not support type 'String'"))
        {
            fail("expected unsupported let type diagnostic");
        }
    }

    std::cout << "OK\n";
    return 0;
}
