#pragma once

#include <curlee/source/span.h>
#include <optional>
#include <string>
#include <vector>

namespace curlee::diag
{

enum class Severity
{
    Error,
    Warning,
    Note,
};

struct Related
{
    std::string message;
    std::optional<curlee::source::Span> span;
};

struct Diagnostic
{
    Severity severity = Severity::Error;
    std::string message;
    std::optional<curlee::source::Span> span;
    std::vector<Related> notes;
};

} // namespace curlee::diag
