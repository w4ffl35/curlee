// SPDX-License-Identifier: MIT
#include <curlee/base/expected.h>
#include <iostream>
#include <string>

using namespace curlee;

Result<int> success()
{
    return 42;
}

Result<int> failure()
{
    return std::unexpected(std::vector<curlee::diag::Diagnostic>{});
}

SingleResult<int> single_success()
{
    return 42;
}

SingleResult<int> single_failure()
{
    return std::unexpected(curlee::diag::Diagnostic{}); // dummy
}

int main()
{
    auto ok = success();
    if (!ok)
        return 1;

    auto bad = failure();
    if (bad)
        return 1;

    auto ok_single = single_success();
    if (!ok_single)
        return 1;

    auto bad_single = single_failure();
    if (bad_single)
        return 1;

    std::cout << "All clear!" << std::endl;
    return 0;
}
