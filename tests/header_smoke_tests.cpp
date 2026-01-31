#include <cstdlib>
#include <curlee/resolver/symbol.h>
#include <curlee/types/type.h>
#include <curlee/types/type_check.h>
#include <iostream>

static void expect_true(bool v, const char* what)
{
    if (!v)
    {
        std::cerr << "FAIL: expected true: " << what << "\n";
        std::exit(1);
    }
}

static void expect_false(bool v, const char* what)
{
    if (v)
    {
        std::cerr << "FAIL: expected false: " << what << "\n";
        std::exit(1);
    }
}

static void expect_eq(std::string_view got, std::string_view expected, const char* what)
{
    if (got != expected)
    {
        std::cerr << "FAIL: " << what << ": got='" << got << "' expected='" << expected << "'\n";
        std::exit(1);
    }
}

int main()
{
    using namespace curlee;

    // resolver::SymbolId comparisons
    {
        resolver::SymbolId a{.value = 1};
        resolver::SymbolId b{.value = 1};
        resolver::SymbolId c{.value = 2};
        expect_true(a == b, "SymbolId ==");
        expect_false(a != b, "SymbolId != false");
        expect_true(a != c, "SymbolId != true");
    }

    // types::Type comparisons and formatting
    {
        const types::Type i{.kind = types::TypeKind::Int};
        const types::Type b{.kind = types::TypeKind::Bool};
        expect_true(i == i, "Type Int == Int");
        expect_false(i == b, "Type Int != Bool");

        const types::Type s1{.kind = types::TypeKind::Struct, .name = "S"};
        const types::Type s2{.kind = types::TypeKind::Struct, .name = "S"};
        const types::Type s3{.kind = types::TypeKind::Struct, .name = "T"};
        expect_true(s1 == s2, "Struct name equality");
        expect_false(s1 == s3, "Struct name inequality");

        expect_eq(types::to_string(types::TypeKind::Int), "Int", "to_string(TypeKind::Int)");
        expect_eq(types::to_string(static_cast<types::TypeKind>(123)), "<unknown>",
                  "to_string(TypeKind invalid)");

        expect_eq(types::to_string(i), "Int", "to_string(Type Int)");
        expect_eq(types::to_string(s1), "S", "to_string(Type Struct)");

        expect_true(types::core_type_from_name("Bool").has_value(), "core_type_from_name Bool");
        expect_false(types::core_type_from_name("NotAType").has_value(),
                     "core_type_from_name unknown");
    }

    // types::TypeInfo::type_of
    {
        types::TypeInfo info;
        expect_false(info.type_of(1).has_value(), "type_of absent");
        info.expr_types.emplace(1, types::Type{.kind = types::TypeKind::Unit});
        const auto got = info.type_of(1);
        expect_true(got.has_value(), "type_of present");
        expect_true(*got == types::Type{.kind = types::TypeKind::Unit}, "type_of value");
    }

    std::cout << "OK\n";
    return 0;
}
