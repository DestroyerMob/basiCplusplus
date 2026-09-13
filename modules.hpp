#pragma once

#include "ast.hpp"

#include <filesystem>

namespace basicc {

struct ModuleResult {
    Program program;
    std::vector<Diagnostic> diagnostics;
    // The build driver uses this list to avoid overwriting any input source.
    std::vector<std::filesystem::path> sources;

    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

// Merge each imported file once, with dependencies before their importers.
// The root was already parsed by the driver; --tokens/--ast stay file-local.
ModuleResult loadModules(Program root, const std::filesystem::path& rootPath);

} // namespace basicc
