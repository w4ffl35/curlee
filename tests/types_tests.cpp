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

    {
        const FunctionType ft{
            .params = {Type{.kind = TypeKind::Int}, Type{.kind = TypeKind::Bool}},
            .result = Type{.kind = TypeKind::Unit},
        };

        const FunctionType same{
            .params = {Type{.kind = TypeKind::Int}, Type{.kind = TypeKind::Bool}},
            .result = Type{.kind = TypeKind::Unit},
        };

        const FunctionType different{
            .params = {Type{.kind = TypeKind::Int}},
            .result = Type{.kind = TypeKind::Unit},
        };

        if (!(ft == same))
        {
            fail("expected function type equality to hold");
        }
        if (ft == different)
        {
            fail("expected different function types to compare unequal");
        }
    }

    {
        const CapabilityType a{.name = "std.fs"};
        const CapabilityType b{.name = "std.fs"};
        const CapabilityType c{.name = "std.net"};

        if (!(a == b))
        {
            fail("expected capability types with the same name to compare equal");
        }
        if (a == c)
        {
            fail("expected capability types with different names to compare unequal");
        }
    }

    std::cout << "OK\n";
    return 0;
}
