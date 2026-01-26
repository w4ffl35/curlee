#pragma once

#include <curlee/diag/diagnostic.h>
#include <curlee/source/source_file.h>
#include <string>

namespace curlee::diag
{

[[nodiscard]] std::string render(const Diagnostic& diagnostic,
                                 const curlee::source::SourceFile& file);

} // namespace curlee::diag
