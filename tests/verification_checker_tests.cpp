// SPDX-License-Identifier: MIT
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
        const std::string source = "fn main(p: cap python.ffi) -> Int {\n"
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

    {
        // Match statements are traversed conservatively by the verifier.
        const std::string source = "enum E { A; B; }\n"
                                   "fn main() -> Int [ ensures result >= 0; ] {\n"
                                   "  match (E::A) {\n"
                                   "    E::A => { return 1; }\n"
                                   "    E::B => { return 2; }\n"
                                   "  }\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "match traversal in verifier");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected verification success for match traversal test");
        }
    }

    {
        // Match arms with null body pointers (manually-shaped AST) are handled conservatively.
        const std::string source = "enum E { A; }\n"
                                   "fn main() -> Int [ ensures result >= 0; ] {\n"
                                   "  match (E::A) {\n"
                                   "    E::A => { return 1; }\n"
                                   "  }\n"
                                   "  return 0;\n"
                                   "}\n";

        auto program = parse_program_or_fail(source, "match traversal null-body verifier test");
        if (program.functions.empty() || program.functions[0].body.stmts.empty())
        {
            fail("expected parsed function with statements");
        }

        auto* match_stmt =
            std::get_if<curlee::parser::MatchStmt>(&program.functions[0].body.stmts[0].node);
        if (match_stmt == nullptr || match_stmt->arms.empty())
        {
            fail("expected first statement to be match with at least one arm");
        }
        match_stmt->arms[0].body.reset();

        const auto typed = curlee::types::type_check(program);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking success for null-body verifier match test");
        }

        const auto& type_info = std::get<curlee::types::TypeInfo>(typed);
        const auto verified = curlee::verification::verify(program, type_info);
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected verification success for null-body verifier match test");
        }
    }

    {
        // Phys<T> is freestanding-only: the verifier treats it as uninterpreted (opaque)
        // and must not error on the type or on phys/read/write expression nodes.
        const std::string source = "fn main(pm: cap phys.mem) -> Unit [ ensures true; ] {\n"
                                   "  unsafe {\n"
                                   "    let fb: Phys<U32> = phys<U32>(0xFD00_0000);\n"
                                   "    fb.write(0xFF8800);\n"
                                   "    let v: U32 = fb.read();\n"
                                   "  }\n"
                                   "  return;\n"
                                   "}\n";

        const auto verified = verify_program(source, "Phys treated as uninterpreted in verifier");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
            for (const auto& d : diags)
            {
                std::cerr << "DIAG: " << d.message << "\n";
            }
            fail("expected verification to succeed for Phys program (uninterpreted type)");
        }
    }

    {
        // Phys<T> read values are opaque: a refinement on a read() result cannot be proven and
        // must fail with the opaque-value note (issue #253, documented restriction).
        const std::string source = "fn main(pm: cap phys.mem) -> Int {\n"
                                   "  unsafe {\n"
                                   "    let reg: Phys<U32> = phys<U32>(0xFE00_0000);\n"
                                   "    let v: U32 where v > 0 = reg.read();\n"
                                   "  }\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "Phys opaque read contract test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for opaque MMIO read contract");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "cannot prove refinement on opaque MMIO read value"))
        {
            fail("expected opaque-read refinement diagnostic");
        }
        if (!any_note_has_substr(diags, "MMIO read is opaque; cannot prove this contract"))
        {
            fail("expected opaque-read note attached to diagnostic");
        }
    }

    {
        // Opaque provenance is transitive: a refinement on an alias of a read() result must
        // also fail (review gap #3).
        const std::string source = "fn main(pm: cap phys.mem) -> Int {\n"
                                   "  unsafe {\n"
                                   "    let reg: Phys<U32> = phys<U32>(0xFE00_0000);\n"
                                   "    let v: U32 = reg.read();\n"
                                   "    let w: U32 = v;\n"
                                   "    let x: U32 where x > 0 = w;\n"
                                   "  }\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "Phys opaque read transitive alias test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for transitive opaque alias refinement");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "cannot prove refinement on opaque MMIO read value"))
        {
            fail("expected opaque-read refinement diagnostic for transitive alias");
        }
        if (!any_note_has_substr(diags, "MMIO read is opaque; cannot prove this contract"))
        {
            fail("expected opaque-read note for transitive alias");
        }
    }

    {
        // An opaque read value must never silently satisfy a requires-guarded call with an
        // unsigned parameter (review gap #1).
        const std::string source = "fn consume(x: U32) -> Int [\n"
                                   "  requires x > 0;\n"
                                   "] {\n"
                                   "  return 0;\n"
                                   "}\n"
                                   "fn main(pm: cap phys.mem) -> Int {\n"
                                   "  unsafe {\n"
                                   "    let reg: Phys<U32> = phys<U32>(0xFE00_0000);\n"
                                   "    let v: U32 = reg.read();\n"
                                   "    return consume(v);\n"
                                   "  }\n"
                                   "}\n";

        const auto verified = verify_program(source, "Phys opaque read requires-call test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for requires-guarded call on opaque read");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "opaque MMIO read"))
        {
            fail("expected opaque-read diagnostic for requires-guarded call");
        }
        if (!any_note_has_substr(diags, "MMIO read is opaque; cannot prove this contract"))
        {
            fail("expected opaque-read note for requires-guarded call");
        }
    }

    {
        // A function returning an opaque read value cannot satisfy `ensures result > 0`
        // (review gap #2).
        const std::string source = "fn read_reg(pm: cap phys.mem) -> U32 [\n"
                                   "  ensures result > 0;\n"
                                   "] {\n"
                                   "  unsafe {\n"
                                   "    let reg: Phys<U32> = phys<U32>(0xFE00_0000);\n"
                                   "    return reg.read();\n"
                                   "  }\n"
                                   "}\n"
                                   "fn main() -> Int {\n"
                                   "  return 0;\n"
                                   "}\n";

        const auto verified = verify_program(source, "Phys opaque read ensures-result test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected verification to fail for ensures on opaque read result");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "cannot prove ensures on opaque MMIO read result"))
        {
            fail("expected opaque-read ensures diagnostic");
        }
        if (!any_note_has_substr(diags, "MMIO read is opaque; cannot prove this contract"))
        {
            fail("expected opaque-read note for ensures");
        }
    }

    // Extern functions (issue #256): an extern declaration with non-trivial
    // contracts is a trusted boundary — the contracts are ASSUMED (a Note is
    // emitted), never proven against the (empty) body, and verification still
    // succeeds.
    {
        const std::string source = R"(
extern fn f(x: Int) -> Int [ requires x > 0; ensures result > 0; ];
fn main() -> Unit { return; }
)";
        auto program = parse_program_or_fail(source, "extern with non-trivial contracts");
        const auto typed = curlee::types::type_check(program);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to succeed for extern with contracts");
        }
        const auto& type_info = std::get<curlee::types::TypeInfo>(typed);

        std::vector<curlee::diag::Diagnostic> notes;
        const auto verified = curlee::verification::verify(program, type_info, &notes);

        // Verification must SUCCEED: the extern contract is assumed, not proven.
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected extern with non-trivial contracts to verify (assumed)");
        }

        // The trust transfer must be surfaced as a Note.
        if (!has_message_substr(notes, "extern boundary: contract assumed, not verified"))
        {
            fail("expected extern boundary Note diagnostic");
        }
    }

    // Extern fn without contracts still gets the boundary Note.
    {
        const std::string source = R"(
extern fn putc(c: Int) -> Unit;
fn main() -> Unit { return; }
)";
        auto program = parse_program_or_fail(source, "extern without contracts");
        const auto typed = curlee::types::type_check(program);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to succeed for extern without contracts");
        }
        const auto& type_info = std::get<curlee::types::TypeInfo>(typed);

        std::vector<curlee::diag::Diagnostic> notes;
        const auto verified = curlee::verification::verify(program, type_info, &notes);
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected extern without contracts to verify");
        }
        if (!has_message_substr(notes, "extern boundary: contract assumed, not verified"))
        {
            fail("expected extern boundary Note for body-less extern");
        }
    }

    // =====================================================================
    // Issue #260 M1: Ghost functions, ghost-let snapshots, and post-state
    // (ensures) inheritance at call sites.
    // =====================================================================

    // Ghost function with scalar signature referenced in an ensures clause
    // lowers to an uninterpreted function application. With no axioms, the
    // uninterpreted call is opaque: an obligation that cannot be proven from
    // facts fails (fail-closed), documenting that ghost bodies are NOT inlined
    // in the M1 fragment.
    {
        const std::string source = R"(
ghost fn g(x: Int) -> Int [ ensures result >= 0; ] {
  return x;
}
fn main() -> Int [ ensures g(1) >= 0; ] {
  return 0;
}
)";
        const auto verified = verify_program(source, "ghost fn uninterpreted call test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected unconstrained ghost fn call to fail verification");
        }
    }

    // Ghost function whose own ensures are provable verifies, even though its
    // body is never emitted. This is the core "ghost code is checked but
    // erased" acceptance criterion.
    {
        const std::string source = R"(
ghost fn g(x: Int) -> Int [ ensures result == x + 1; ] {
  return x + 1;
}
fn main() -> Int [ ensures true; ] {
  return 0;
}
)";
        const auto verified = verify_program(source, "ghost fn provable ensures test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected ghost fn with provable ensures to verify");
        }
    }

    // Ghost function calls in predicates lower to uninterpreted functions: a
    // predicate obligation that is an equality with itself (g(1) == g(1)) is
    // trivially provable regardless of the uninterpreted function's value.
    {
        const std::string source = R"(
ghost fn g(x: Int) -> Int [ ensures result == x + 1; ] {
  return x + 1;
}
fn main() -> Int [ ensures g(1) == g(1); ] {
  return 0;
}
)";
        const auto verified = verify_program(source, "ghost fn predicate reflexivity test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected reflexive ghost call predicate to verify");
        }
    }

    // Ghost let snapshot: `ghost let snap = x;` freezes the current value of x
    // into a fresh solver constant. A later predicate that references `snap`
    // sees the frozen value (snap == x, and x == 5, so snap == 5 is provable).
    {
        const std::string source = R"(
fn main() -> Int [ ensures result == 5; ] {
  let x: Int = 5;
  ghost let snap = x;
  return snap;
}
)";
        const auto verified = verify_program(source, "ghost let snapshot test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected ghost let snapshot program to verify");
        }
    }

    // Post-state inheritance: after a call passes its requires, the callee's
    // ensures become caller-side facts. `inc(x)` ensures `result > x`; a
    // caller that must prove `y > x` after `let y = inc(x);` verifies.
    {
        const std::string source = R"(
fn inc(x: Int) -> Int [ ensures result > x; ] {
  return x + 1;
}
fn main() -> Int [ ensures result > 0; ] {
  let x: Int = 5;
  let y: Int = inc(x);
  return y;
}
)";
        const auto verified = verify_program(source, "ensures inheritance call-site test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected ensures inheritance to make caller obligation provable");
        }
    }

    // Negative control: a caller obligation that requires a callee post-state
    // the callee does NOT provide must still fail (ensures inheritance is not
    // a blanket "anything holds" rule).
    {
        const std::string source = R"(
fn id(x: Int) -> Int { return x; }
fn main() -> Int [ ensures result > 5; ] {
  let x: Int = 5;
  let y: Int = id(x);
  return y;
}
)";
        const auto verified = verify_program(source, "no-ensures inheritance control test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected call without ensures to fail caller obligation");
        }
    }

    // Contract-block ghost snapshot (roadmap canonical pattern): the snapshot
    // name declared in the `[...]` block is visible to the function's own
    // `ensures`. `transfer` guarantees `result == src_before` where
    // `src_before` froze the pre-state value of `src`; returning `src`
    // satisfies it.
    {
        const std::string source = R"(
fn transfer(src: Int, dst: Int) -> Int
  [ ghost let src_before = src;
    ensures result == src_before; ]
{
  return src;
}
fn main() -> Int { return transfer(5, 0); }
)";
        const auto verified = verify_program(source, "contract-block snapshot positive test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected contract-block snapshot ensures to verify");
        }
    }

    // Negative control: an `ensures` referencing a snapshot name that is NOT
    // declared in the contract block must fail (unknown name).
    {
        const std::string source = R"(
fn f(x: Int) -> Int [ ensures result == not_declared; ] { return x; }
fn main() -> Int { return f(1); }
)";
        const auto verified = verify_program(source, "contract-block snapshot negative test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected unknown snapshot name in ensures to fail");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        // The verifier's predicate lowering rejects an undeclared snapshot name.
        if (!has_message_substr(diags, "unknown predicate name 'not_declared'"))
        {
            fail("expected unknown predicate name diagnostic for undeclared snapshot");
        }
    }

    // Call-site inheritance of callee snapshots: the caller can discharge its
    // own obligation using the callee's inherited ensures, which references the
    // callee's snapshot re-lowered to the caller's argument.
    {
        const std::string source = R"(
fn transfer(src: Int, dst: Int) -> Int
  [ ghost let src_before = src;
    ensures result == src_before; ]
{
  return src;
}
fn main() -> Int [ ensures result == 5; ] {
  let y: Int = transfer(5, 0);
  return y;
}
)";
        const auto verified = verify_program(source, "call-site callee snapshot inheritance test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected call-site inheritance of callee snapshot to verify");
        }
    }

    // Callee-unprovable-ensures isolation: a callee whose own ensures are
    // unprovable from its body must itself fail, so the inherited fact is never
    // trusted from an unverified callee.
    {
        const std::string source = R"(
fn bad(x: Int) -> Int [ ensures result > 100; ] { return x; }
fn main() -> Int [ ensures result > 100; ] {
  let y: Int = bad(5);
  return y;
}
)";
        const auto verified = verify_program(source, "callee-unprovable-ensures isolation test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected unprovable callee ensures to fail (isolation)");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(verified);
        if (!has_message_substr(diags, "ensures clause not satisfied"))
        {
            fail("expected ensures failure diagnostic for unprovable callee");
        }
    }

    // Snapshot stability under rebinding: the contract-block snapshot freezes
    // the pre-state value; a later re-binding of the source in a nested scope
    // does not affect the snapshot. (The language has no `var`/assignment yet,
    // so this demonstrates the snapshot semantics under the actual rebinding
    // semantics available today.)
    {
        const std::string source = R"(
fn f() -> Int
  [ ghost let snap = 5;
    ensures result == snap; ]
{
  let x: Int = 0;   // rebind in a nested scope; snap is unaffected
  return 5;
}
fn main() -> Int { return f(); }
)";
        const auto verified = verify_program(source, "snapshot rebinding stability test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected snapshot to survive a later source rebinding");
        }
    }

    // Soundness regression: a caller local whose name collides with the callee's
    // snapshot name must NOT let the inherited ensures resolve the snapshot to
    // the caller's value. The callee's snapshot always wins, so `ensures
    // result == 99` (false at runtime: y == 5) must fail.
    {
        const std::string source = R"(
fn t(x: Int) -> Int
  [ ghost let snap = x;
    ensures result == snap; ]
{
  return x;
}
fn main() -> Int [ ensures result == 99; ] {
  let snap: Int = 99;   // caller local with the same name as the callee snapshot
  let y: Int = t(5);    // y == 5 at runtime
  return y;
}
)";
        const auto verified = verify_program(source, "snapshot-name collision caller-local test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected caller-local snapshot-name collision to fail (soundness)");
        }
    }

    // Soundness regression: a caller contract-block ghost let whose name
    // collides with the callee's snapshot name must also fail (the callee's
    // snapshot, bound from the caller's argument, wins).
    {
        const std::string source = R"(
fn t(x: Int) -> Int
  [ ghost let snap = x;
    ensures result == snap; ]
{
  return x;
}
fn main() -> Int
  [ ghost let snap = 100;
    ensures result == snap; ]
{
  let y: Int = t(5);    // y == 5 at runtime
  return y;
}
)";
        const auto verified = verify_program(source, "snapshot-name collision caller-ghost-let test");
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
        {
            fail("expected caller ghost-let snapshot-name collision to fail (soundness)");
        }
    }

    // Positive control for the collision fix: with NO collision, the caller
    // proves its obligation via the callee's inherited ensures + snapshot.
    {
        const std::string source = R"(
fn t(x: Int) -> Int
  [ ghost let snap = x;
    ensures result == snap; ]
{
  return x;
}
fn main() -> Int [ ensures result == 5; ] {
  let y: Int = t(5);
  return y;
}
)";
        const auto verified = verify_program(source, "snapshot collision positive control");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected collision-free inherited snapshot ensures to verify");
        }
    }

    // A callee whose `requires` references its own contract-block snapshot must
    // be provable at the call site too (the resolver permits `requires` to use
    // snapshot names; the call-site lowering must make them visible).
    {
        const std::string source = R"(
fn t(x: Int) -> Int
  [ ghost let snap = x;
    requires snap >= 0;
    ensures result == snap; ]
{
  return x;
}
fn main() -> Int { return t(5); }
)";
        const auto verified = verify_program(source, "requires-snapshot call-site test");
        if (!std::holds_alternative<curlee::verification::Verified>(verified))
        {
            fail("expected requires referencing own snapshot to verify at call site");
        }
    }

    std::cout << "OK\n";
    return 0;
}
