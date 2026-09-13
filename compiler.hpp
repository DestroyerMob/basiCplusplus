#pragma once

#include "ast.hpp"

namespace basicc {

struct CompileResult {
    std::string cSource;
    std::string cppSource; // Optional C ABI wrappers, compiled separately as C++17.
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

// Check the executable subset, then lower it to C and optional C++ bridges. The AST stays intact
// so --ast remains useful even for programs that do not pass semantic checks.
CompileResult compile(const Program& program);

} // namespace basicc
