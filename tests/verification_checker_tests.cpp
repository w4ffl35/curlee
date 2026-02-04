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

static bool any_note_has_substr(const std::vector<curlee::diag::Diagnostic>& diags,
                                std::string_view needle)
{
    for (const auto& d : diags)
    {
        for (const auto& n : d.notes)
        {
            if (n.message.find(needle) != std::string::npos)
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

    {
        // Predicate rendering: bool literals stringify as "true"/"false".
        const std::string source = "fn main() -> Int [\n"
                                   "  ensures true && false;\n"
                                   "] {\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "bool literal predicate rendering test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for bool literal predicate rendering test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "ensures clause not satisfied"))
        {
            fail("expected ensures failure diagnostic for bool literal predicate rendering test");
        }
        if (!any_note_has_prefix(diags, "goal: (true && false)"))
        {
            fail("expected goal note to include rendered bool literals for bool literal predicate");
        }
    }

    {
        // Bool literals in runtime expressions are supported by verifier lowering.
        const std::string source = "fn bad_bool() -> Bool [\n"
                                   "  ensures result;\n"
                                   "] {\n"
                                   "  return false;\n"
                                   "}\n"
                                   "fn main() -> Int { return 0; }\n";

        const auto verified = verify_program(source, "bool literal return lowering test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for bool literal return lowering test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "ensures clause not satisfied"))
        {
            fail("expected ensures failure diagnostic for bool literal return lowering test");
        }
        if (!any_note_has_prefix(diags, "goal: result"))
        {
            fail("expected goal note 'goal: result' for bool literal return lowering test");
        }
    }

    {
        // Bool result modeling: include result in model vars for Bool results.
        // Use a non-literal Bool expression so lowering succeeds.
        const std::string source = "fn bad_bool() -> Bool [\n"
                                   "  ensures result;\n"
                                   "] {\n"
                                   "  return 0 == 1;\n"
                                   "}\n"
                                   "fn main() -> Int { return 0; }\n";

        const auto verified = verify_program(source, "bool result model var test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for bool result model var test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "ensures clause not satisfied"))
        {
            fail("expected ensures failure diagnostic for bool result model var test");
        }
        if (!any_note_has_prefix(diags, "goal: result"))
        {
            fail("expected goal note 'goal: result' for bool result model var test");
        }
        if (!any_note_has_prefix(diags, "model:"))
        {
            fail("expected model note for bool result model var test");
        }
    }

    {
        // Goal string coverage: exercise a variety of operators in goal rendering.
        const std::string source = "fn f() -> Int [\n"
                                   "  ensures ((result > 0) && !(result != 0)) || (result < 0);\n"
                                   "] {\n"
                                   "  return 0;\n"
                                   "}\n"
                                   "fn main() -> Int { return f(); }\n";

        const auto verified = verify_program(source, "goal operator rendering test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for goal operator rendering test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "ensures clause not satisfied"))
        {
            fail("expected ensures failure diagnostic for goal operator rendering test");
        }
        if (!any_note_has_prefix(diags, "goal: "))
        {
            fail("expected a goal note for goal operator rendering test");
        }
        if (!any_note_has_substr(diags, "&&") || !any_note_has_substr(diags, "||") ||
            !any_note_has_substr(diags, "!=") || !any_note_has_substr(diags, "!") ||
            !any_note_has_substr(diags, ">") || !any_note_has_substr(diags, "<"))
        {
            fail("expected goal note to include rendered operators for goal operator rendering "
                 "test");
        }
    }

    {
        // Bool parameter model vars: include param bool vars in model rendering.
        const std::string source = "fn take_true(b: Bool) -> Int [\n"
                                   "  requires b == true;\n"
                                   "] {\n"
                                   "  return 0;\n"
                                   "}\n"
                                   "fn main() -> Int {\n"
                                   "  return take_true(false);\n"
                                   "}\n";

        const auto verified = verify_program(source, "bool param model var test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for bool param model var test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "requires clause not satisfied"))
        {
            for (const auto& d : diags)
            {
                std::cerr << "diag: " << d.message << "\n";
                for (const auto& n : d.notes)
                {
                    std::cerr << "note: " << n.message << "\n";
                }
            }
            fail("expected requires failure diagnostic for bool param model var test");
        }
        if (!any_note_has_prefix(diags, "goal: ") || !any_note_has_substr(diags, "b") ||
            !any_note_has_substr(diags, "=="))
        {
            fail("expected goal note to include b and == for bool param model var test");
        }
        if (!any_note_has_prefix(diags, "model:"))
        {
            fail("expected model note for bool param model var test");
        }
    }

    {
        // If/else checking: cover path-fact injection for then/else branches.
        // The else branch returns 0, which violates ensures result > 0.
        const std::string source = "fn f(x: Int) -> Int [\n"
                                   "  ensures result > 0;\n"
                                   "] {\n"
                                   "  if (x > 0) {\n"
                                   "    return 1;\n"
                                   "  } else {\n"
                                   "    return 0;\n"
                                   "  }\n"
                                   "}\n"
                                   "fn main() -> Int { return f(0); }\n";

        const auto verified = verify_program(source, "if/else fact injection test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for if/else fact injection test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "ensures clause not satisfied"))
        {
            fail("expected ensures failure diagnostic for if/else fact injection test");
        }
        if (!any_note_has_prefix(diags, "goal: "))
        {
            fail("expected goal note for if/else fact injection test");
        }
    }

    {
        // While checking: cover condition lowering and loop body traversal.
        // The call in the body should trigger call-site checking.
        const std::string source = "fn take_true(b: Bool) -> Int [\n"
                                   "  requires b == true;\n"
                                   "] {\n"
                                   "  return 0;\n"
                                   "}\n"
                                   "fn main() -> Int {\n"
                                   "  while (0 == 0) {\n"
                                   "    take_true(false);\n"
                                   "    return 0;\n"
                                   "  }\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "while condition traversal test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for while condition traversal test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "requires clause not satisfied"))
        {
            fail("expected requires failure diagnostic for while condition traversal test");
        }
        if (!any_note_has_prefix(diags, "goal: "))
        {
            fail("expected goal note for while condition traversal test");
        }
    }

    {
        // Non-scalar let without refinements is allowed.
        const std::string source = "struct S { x: Int; }\n"
                                   "fn main() -> Int {\n"
                                   "  let s: S = S{ x: 1 };\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "non-scalar let without refinement test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected verification to succeed for non-scalar let without refinement test");
        }
    }

    {
        // Expr call-walk coverage: traverse Unary/Binary/Group nodes.
        const std::string source = "fn main() -> Int {\n"
                                   "  let b: Bool = !((0 == 1) && (1 == 1));\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "expr traversal unary/binary/group test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected verification to succeed for expr traversal unary/binary/group test");
        }
    }

    {
        // Scalar let refinements: a refinement on an Int let introduces a fact.
        const std::string source = "fn main() -> Int [ ensures result > 0; ] {\n"
                                   "  let x: Int where x > 0 = 1;\n"
                                   "  return x;\n"
                                   "}\n";

        const auto verified = verify_program(source, "scalar let refinement fact test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected verification to succeed for scalar let refinement fact test");
        }
    }

    {
        // Predicate lowering failure in requires clauses should surface as a diagnostic.
        const std::string source = "fn f(x: Int) -> Int [ requires y > 0; ] {\n"
                                   "  return x;\n"
                                   "}\n"
                                   "fn main() -> Int { return f(0); }\n";

        const auto verified = verify_program(source, "unknown name in requires test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for unknown name in requires test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "unknown predicate name"))
        {
            fail("expected unknown name diagnostic for requires predicate lowering");
        }
    }

    {
        // Expression lowering: MemberExpr is not supported by the solver-side expression
        // language, even if the program type-checks.
        const std::string source = "struct S { x: Int; }\n"
                                   "fn f(x: Int) -> Int [ requires x > 0; ] { return x; }\n"
                                   "fn main() -> Int {\n"
                                   "  let s: S = S{ x: 1 };\n"
                                   "  f(s.x);\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "member expr unsupported in call arg test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for member expr unsupported in call arg test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "unsupported expression in verification"))
        {
            fail("expected unsupported expression diagnostic for member expr call arg");
        }
    }

    {
        // Expression lowering: MemberExpr in return position should surface an error.
        const std::string source = "struct S { x: Int; }\n"
                                   "fn bad() -> Int [ ensures result > 0; ] {\n"
                                   "  let s: S = S{ x: 0 };\n"
                                   "  return s.x;\n"
                                   "}\n"
                                   "fn main() -> Int { return bad(); }\n";

        const auto verified = verify_program(source, "member expr unsupported in return test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for member expr unsupported in return test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "unsupported expression in verification"))
        {
            fail("expected unsupported expression diagnostic for member expr return");
        }
    }

    {
        // python_ffi.call(...) should not be treated as a verifier-checked call.
        const std::string source = "fn main() -> Int {\n"
                                   "  unsafe { python_ffi.call(); }\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "python_ffi.call skip call-site check");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected verification to succeed for python_ffi.call skip call-site check");
        }
    }

    {
        // If/else path-sensitivity: the verifier checks both branches, but it should also
        // add a condition fact to each branch scope. This makes unreachable branches (under
        // the added fact) safe even if they contain failing call-site obligations.
        const std::string source =
            "fn req_pos(x: Int) -> Int [ requires x > 0; ] { return x; }\n"
            "fn main() -> Int {\n"
            "  if (!(true && (false || true))) {\n"
            "    // Condition is logically false; this branch gets a false fact.\n"
            "    req_pos(0);\n"
            "  } else {\n"
            "    req_pos(1);\n"
            "  }\n"
            "  return 0;\n"
            "}\n";

        const auto verified = verify_program(source, "if/else path-sensitive facts test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected verification to succeed for if/else path-sensitive facts test");
        }
    }

    {
        // Unsafe statements should introduce a nested scope for facts/vars, but still check calls.
        const std::string source = "fn req_pos(x: Int) -> Int [ requires x > 0; ] { return x; }\n"
                                   "fn main() -> Int {\n"
                                   "  unsafe { req_pos(1); }\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "unsafe scope call checking test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected verification to succeed for unsafe scope call checking test");
        }
    }

    {
        // While condition lowering: calls are not supported in solver-side expressions, so the
        // condition fact will be absent; still, call checking should run and not crash.
        const std::string source = "fn cond() -> Bool { return true; }\n"
                                   "fn req_pos(x: Int) -> Int [ requires x > 0; ] { return x; }\n"
                                   "fn main() -> Int {\n"
                                   "  while (cond()) {\n"
                                   "    req_pos(1);\n"
                                   "    return 0;\n"
                                   "  }\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "while condition call lowering test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected verification to succeed for while condition call lowering test");
        }
    }

    {
        // Expression lowering: '/' is not supported by the solver-side expression language.
        const std::string source = "fn bad_div(x: Int) -> Int [ ensures result > 0; ] {\n"
                                   "  return x / 2;\n"
                                   "}\n"
                                   "fn main() -> Int { return bad_div(1); }\n";

        const auto verified = verify_program(source, "division unsupported in verification expr");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for division unsupported in verification expr");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "unsupported binary operator in expression"))
        {
            fail("expected unsupported binary operator diagnostic for division");
        }
    }

    {
        // Goal rendering: exercise <=, >=, +, -, * in goal notes.
        const std::string source = "fn arith(x: Int) -> Int [\n"
                                   "  requires (x + 1) <= 0;\n"
                                   "  requires (x - 1) >= 0;\n"
                                   "  requires (x * 2) >= 0;\n"
                                   "] {\n"
                                   "  return x;\n"
                                   "}\n"
                                   "fn main() -> Int {\n"
                                   "  arith(0);\n"
                                   "  arith(-1);\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "goal <= >= + - * rendering test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for goal <= >= + - * rendering test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "requires clause not satisfied"))
        {
            fail("expected requires failure for goal <= >= + - * rendering test");
        }
        if (!any_note_has_prefix(diags, "goal: "))
        {
            fail("expected goal note for goal <= >= + - * rendering test");
        }
        if (!any_note_has_substr(diags, "<=") || !any_note_has_substr(diags, ">=") ||
            !any_note_has_substr(diags, "+") || !any_note_has_substr(diags, "-") ||
            !any_note_has_substr(diags, "*"))
        {
            fail("expected goal note to include <=, >=, +, -, * for goal <= >= + - * rendering "
                 "test");
        }
    }

    {
        // Expression lowering: unknown name errors should point at the name.
        const std::string source = "fn f(x: Int) -> Int [\n"
                                   "  ensures x > y;\n"
                                   "] {\n"
                                   "  return x;\n"
                                   "}\n"
                                   "fn main() -> Int { return f(0); }\n";

        const auto verified = verify_program(source, "unknown name in ensures expr lowering");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for unknown name in ensures expr lowering");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "unknown predicate name") &&
            !has_message_substr(diags, "unknown name 'y'"))
        {
            fail("expected unknown name diagnostic for y");
        }
    }

    {
        // Bool var lookup: ensure NameExpr lowering finds bool vars.
        const std::string source = "fn f(b: Bool) -> Int [ ensures b; ] { return 0; }\n"
                                   "fn main() -> Int { return f(false); }\n";

        const auto verified = verify_program(source, "bool NameExpr lowering");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for bool NameExpr lowering");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "ensures clause not satisfied"))
        {
            fail("expected ensures failure for bool NameExpr lowering");
        }
        if (!any_note_has_prefix(diags, "goal: ") || !any_note_has_substr(diags, "b"))
        {
            fail("expected goal note to mention b for bool NameExpr lowering");
        }
    }

    {
        // BlockStmt: explicit blocks should introduce a verifier scope.
        const std::string source = "fn main() -> Int {\n"
                                   "  { let x: Int = 0; }\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "BlockStmt scope push/pop");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected verification to succeed for BlockStmt scope push/pop");
        }
    }

    {
        // Ensure verifier call-scanning visits ScopedNameExpr nodes.
        const std::string source = "enum E { A; }\n"
                                   "fn main() -> Int [ ensures result == 0; ] {\n"
                                   "  let e: E = E::A;\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "ScopedNameExpr visited in verifier");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected verification to succeed for ScopedNameExpr visitor coverage");
        }
    }

    std::cout << "OK\n";
    return 0;
}
