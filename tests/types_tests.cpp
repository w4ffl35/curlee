#include <curlee/types/type.h>
#include <cstdlib>
#include <iostream>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

int main()
{
    using namespace curlee::types;

    if (core_type_from_name("Int") != Type{.kind = TypeKind::Int})
    {
        fail("expected Int core type");
    }
    if (core_type_from_name("Bool") != Type{.kind = TypeKind::Bool})
    {
        fail("expected Bool core type");
    }
    if (core_type_from_name("String") != Type{.kind = TypeKind::String})
    {
        fail("expected String core type");
    }
    if (core_type_from_name("Unit") != Type{.kind = TypeKind::Unit})
    {
        fail("expected Unit core type");
    }
    if (core_type_from_name("Nope").has_value())
    {
        fail("expected unknown type name to map to nullopt");
    }

    std::cout << "OK\n";
    return 0;
}
