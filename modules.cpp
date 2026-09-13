#include "modules.hpp"

#include "lexer.hpp"
#include "parser.hpp"

#include <fstream>
#include <iterator>
#include <unordered_map>

namespace basicc {
namespace {

namespace fs = std::filesystem;
enum class Visit { Loading, Loaded };

class Loader {
public:
    ModuleResult run(Program root, const fs::path& rootPath) {
        try {
            // weakly_canonical also supports an already-read stream root such
            // as /dev/stdin; imported files themselves must exist on disk.
            const auto path = fs::weakly_canonical(fs::absolute(rootPath));
            states_.emplace(path.string(), Visit::Loading);
            result_.sources.push_back(path);
            merge(std::move(root), path);
        } catch (const fs::filesystem_error& error) {
            result_.diagnostics.push_back({{1, 1, rootPath.string()}, error.what()});
        }
        return std::move(result_);
    }

private:
    bool loadImport(const ImportDeclaration& declaration, const fs::path& directory) {
        const std::string requested = decodeStringLiteral(declaration.path.lexeme);
        const SourceLocation location = declaration.path.location;
        if (requested.empty() || requested.find('\0') != std::string::npos) {
            result_.diagnostics.push_back({location, "import path must be nonempty and contain no NUL bytes"});
            return false;
        }
        try {
            const auto path = fs::canonical(directory / fs::path(requested));
            const auto found = states_.find(path.string());
            if (found != states_.end()) {
                if (found->second == Visit::Loaded) return true;
                result_.diagnostics.push_back({location, "import cycle involving '" + path.string() + "'"});
                return false;
            }
            if (!fs::is_regular_file(path)) {
                result_.diagnostics.push_back({location, "import must refer to a regular source file"});
                return false;
            }
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                result_.diagnostics.push_back({location, "could not open imported file '" + path.string() + "'"});
                return false;
            }
            const std::string source{std::istreambuf_iterator<char>(input), {}};
            if (input.bad()) {
                result_.diagnostics.push_back({location, "could not read imported file '" + path.string() + "'"});
                return false;
            }
            Lexer lexer(source, path.string());
            auto lexed = lexer.lex();
            if (!lexed.ok()) {
                result_.diagnostics = std::move(lexed.diagnostics);
                return false;
            }
            Parser parser(lexed.tokens);
            auto parsed = parser.parse();
            if (!parsed.ok()) {
                result_.diagnostics = std::move(parsed.diagnostics);
                return false;
            }
            states_.emplace(path.string(), Visit::Loading);
            result_.sources.push_back(path);
            return merge(std::move(parsed.program), path);
        } catch (const fs::filesystem_error& error) {
            result_.diagnostics.push_back({location, "could not load import '" + requested + "': " + error.what()});
            return false;
        }
    }

    bool merge(Program program, const fs::path& path) {
        // Resolve imports first regardless of where they appear in the file.
        // A shared dependency contributes its globals and functions only once.
        for (const auto& declaration : program.declarations) {
            if (declaration->kind == DeclarationKind::Import &&
                !loadImport(static_cast<const ImportDeclaration&>(*declaration), path.parent_path())) return false;
        }
        for (auto& declaration : program.declarations) {
            if (declaration->kind != DeclarationKind::Import) {
                result_.program.declarations.push_back(std::move(declaration));
            }
        }
        states_.at(path.string()) = Visit::Loaded;
        return true;
    }

    ModuleResult result_;
    std::unordered_map<std::string, Visit> states_;
};

} // namespace

ModuleResult loadModules(Program root, const fs::path& rootPath) {
    return Loader().run(std::move(root), rootPath);
}

} // namespace basicc
