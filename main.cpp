#include "ast_printer.hpp"
#include "compiler.hpp"
#include "lexer.hpp"
#include "modules.hpp"
#include "parser.hpp"

#include <fstream>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace {

enum class OutputMode {
    Tokens,
    Ast,
    Check,
    EmitC,
    EmitCpp,
    EmitIR,
    Build,
};

struct BuildOptions {
    std::string backend = "c";
    std::string optimization = "-O0";
    std::vector<std::filesystem::path> cppHeaders;
};

std::string escapeLexeme(std::string_view lexeme) {
    std::string escaped;
    escaped.reserve(lexeme.size());

    for (const char character : lexeme) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        case '\0':
            escaped += "\\0";
            break;
        default:
            escaped += character;
            break;
        }
    }

    return escaped;
}

void printUsage(std::ostream& output) {
    output << "usage: basicc (--tokens|--ast|--check|--emit-c) <file.bc>\n"
              "       basicc --emit-cpp <file.bc> [--cpp-header <file>]\n"
              "       basicc --emit-ir <file.bc> [-O0|-O1|-O2|-O3]\n"
              "       basicc build <file.bc> [-o <executable>] [--link <file>] [-l <library>]\n"
              "                    [--backend c|llvm] [-O0|-O1|-O2|-O3] [--cpp-header <file>]\n";
}

// Pass an argument vector directly to the compiler. Source and output paths
// never become shell commands, even when they contain spaces or punctuation.
int runCompiler(const std::vector<std::string>& arguments, const char* setting = "CC") {
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
#ifdef _WIN32
    const auto status = _spawnvp(_P_WAIT, argv[0], argv.data());
    if (status != -1) return static_cast<int>(status);
    const int error = errno;
#else
    pid_t process;
    const int error = posix_spawnp(&process, argv[0], nullptr, nullptr, argv.data(), environ);
    if (error == 0) {
        int status;
        while (waitpid(process, &status, 0) == -1) {
            if (errno == EINTR) continue;
            std::cerr << "error: could not wait for compiler: " << std::strerror(errno) << '\n';
            return 1;
        }
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
#endif
    std::cerr << "error: could not start compiler '" << arguments.front()
              << "': " << std::strerror(error)
              << ". Install the compiler or set " << setting << " to its executable path.\n";
    return 1;
}

std::string compilerExecutable(const char* setting, const char* fallback) {
    const char* configured = std::getenv(setting);
    return configured && *configured ? configured : fallback;
}

bool runStage(const std::vector<std::string>& arguments, const char* stage,
              const char* setting = "CC") {
    const int status = runCompiler(arguments, setting);
    if (status == 0) return true;
    std::cerr << "error: " << stage << " failed (exit " << status << ")\n";
    return false;
}

// Both IR inspection and builds need scratch files. Scope-based cleanup covers
// every failed compiler stage without touching the user's previous executable.
class WorkDirectory {
public:
    WorkDirectory(const WorkDirectory&) = delete;
    WorkDirectory& operator=(const WorkDirectory&) = delete;
    explicit WorkDirectory(const std::filesystem::path& parent) {
        std::random_device random;
        for (int attempt = 0; attempt < 20; ++attempt) {
            const auto candidate = parent / (".basicc-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                path = candidate;
                return;
            }
        }
        throw std::runtime_error("could not create build directory");
    }
    ~WorkDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
};

void writeSource(const std::filesystem::path& path, const std::string& source) {
    std::ofstream output(path, std::ios::binary);
    output << source;
    output.close();
    if (!output) throw std::runtime_error("could not write generated source");
}

std::string cppSource(const std::string& wrappers, const BuildOptions& options) {
    std::string result;
    for (const auto& header : options.cppHeaders) {
        const auto path = std::filesystem::canonical(header);
        if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("C++ header is not a regular file: " + path.string());
        const auto name = path.generic_string();
        // Header names are preprocessor tokens, not escaped string literals.
        if (name.find_first_of("\"\r\n") != std::string::npos) {
            throw std::runtime_error("C++ header paths cannot contain quotes or newlines");
        }
        result += "#include \"" + name + "\"\n";
    }
    return result + wrappers;
}

bool lowerToIR(const std::filesystem::path& source, const std::string& output,
               const BuildOptions& options) {
    // Clang supplies ABI lowering and optimization for the checked C form.
    // LLVM IR becomes the input to the separate native-code generation stage.
    return runStage({compilerExecutable("BASICC_CLANG", "clang"), "-std=c11",
                     options.optimization, "-S", "-emit-llvm", source.string(), "-o", output},
                    "LLVM IR generation", "BASICC_CLANG");
}

bool emitIR(const std::string& source, const BuildOptions& options) {
    try {
        const WorkDirectory work(std::filesystem::temp_directory_path());
        const auto cFile = work.path / "program.c";
        writeSource(cFile, source);
        return lowerToIR(cFile, "-", options); // Keep stdout exclusively for IR.
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return false;
    }
}

bool buildExecutable(const basicc::CompileResult& compiled, const std::vector<std::filesystem::path>& inputs,
                     std::filesystem::path output,
                     const std::vector<std::filesystem::path>& linkedFiles,
                     const std::vector<std::string>& libraries,
                     const BuildOptions& options) {
    namespace fs = std::filesystem;
    try {
        output = fs::absolute(output).lexically_normal();
        auto protectedInputs = inputs;
        protectedInputs.insert(protectedInputs.end(), options.cppHeaders.begin(), options.cppHeaders.end());
        const auto bridgeSource = cppSource(compiled.cppSource, options);
        std::vector<std::string> linkArguments;
        for (const auto& linked : linkedFiles) {
            // Absolute paths cannot be mistaken for compiler options, including
            // names beginning with '-' or '@'. Preserve the user's link order.
            const auto path = fs::canonical(linked);
            if (!fs::is_regular_file(path)) throw std::runtime_error("link input is not a regular file: " + path.string());
            protectedInputs.push_back(path);
            linkArguments.push_back(path.string());
        }
        for (const auto& input : protectedInputs) {
            if (output == fs::absolute(input).lexically_normal() ||
                (fs::exists(output) && fs::equivalent(input, output))) {
                std::cerr << "error: executable output must not overwrite a source file\n";
                return false;
            }
        }
        // Build beside the destination so the final rename stays on one file
        // system. A failed build leaves any previous executable untouched.
        const WorkDirectory work(output.parent_path());
        const auto cFile = work.path / "program.c";
        const auto executable = work.path / "program.exe";
        writeSource(cFile, compiled.cSource);
        const auto nativeInput = work.path / "program.o";
        if (options.backend == "llvm") {
            const auto irFile = work.path / "program.ll";
            if (!lowerToIR(cFile, irFile.string(), options)) return false;
            if (!runStage({compilerExecutable("BASICC_CLANG", "clang"), options.optimization,
                           "-c", irFile.string(), "-o", nativeInput.string()},
                          "LLVM native-code generation", "BASICC_CLANG")) return false;
        } else if (!runStage({compilerExecutable("CC", "cc"), "-std=c11", options.optimization,
                              "-c", cFile.string(), "-o", nativeInput.string()}, "C compilation")) {
            return false;
        }
        bool needsCpp = !compiled.cppSource.empty() || !options.cppHeaders.empty();
        std::vector<std::string> objects{nativeInput.string()};
        if (needsCpp) {
            const auto bridge = work.path / "bridge.cpp";
            const auto object = work.path / "bridge.o";
            writeSource(bridge, bridgeSource);
            if (!runStage({compilerExecutable("CXX", "c++"), "-std=c++17", options.optimization,
                           "-c", bridge.string(), "-o", object.string()}, "C++ bridge compilation", "CXX")) return false;
            objects.push_back(object.string());
        }
        // Compile source inputs in their own language before linking. In
        // particular, a C++ linker must never reinterpret generated C as C++.
        for (std::size_t i = 0; i < linkArguments.size(); ++i) {
            const auto extension = fs::path(linkArguments[i]).extension().string();
            const bool cpp = extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".C";
            if (!cpp && extension != ".c") continue;
            needsCpp = needsCpp || cpp;
            const auto object = work.path / ("linked-" + std::to_string(i) + ".o");
            const char* setting = cpp ? "CXX" : "CC";
            if (!runStage({compilerExecutable(setting, cpp ? "c++" : "cc"), cpp ? "-std=c++17" : "-std=c11",
                           options.optimization, "-c", linkArguments[i], "-o", object.string()},
                          "linked source compilation", setting)) return false;
            linkArguments[i] = object.string();
        }
        const char* linkerSetting = needsCpp ? "CXX" : "CC";
        std::vector<std::string> arguments{compilerExecutable(linkerSetting, needsCpp ? "c++" : "cc"),
                                           options.optimization};
        arguments.insert(arguments.end(), objects.begin(), objects.end());
        arguments.insert(arguments.end(), linkArguments.begin(), linkArguments.end());
        for (const auto& library : libraries) arguments.push_back("-l" + library);
        arguments.insert(arguments.end(), {"-o", executable.string()});
        if (!runStage(arguments, "linking", linkerSetting)) return false;
        fs::rename(executable, output);
        std::cout << "Built " << output.string() << '\n';
        return true;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return false;
    }
}

void printTokens(const std::vector<basicc::Token>& tokens) {
    for (const basicc::Token& token : tokens) {
        std::cout << token.location.line << ':' << token.location.column << ' '
                  << basicc::tokenTypeName(token.type) << " \""
                  << escapeLexeme(token.lexeme) << "\"\n";
    }
}

void printDiagnostics(const std::string& path,
                      const std::vector<basicc::Diagnostic>& diagnostics) {
    for (const basicc::Diagnostic& diagnostic : diagnostics) {
        std::cerr << (diagnostic.location.file.empty() ? path : diagnostic.location.file)
                  << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column << ": error: "
                  << diagnostic.message << '\n';
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        printUsage(std::cout);
        return 0;
    }
    if (argc < 3) {
        printUsage(std::cerr);
        return 2;
    }

    OutputMode mode;
    const std::string_view modeArgument = argv[1];
    if (modeArgument == "--tokens") {
        mode = OutputMode::Tokens;
    } else if (modeArgument == "--ast") {
        mode = OutputMode::Ast;
    } else if (modeArgument == "--check") {
        mode = OutputMode::Check;
    } else if (modeArgument == "--emit-c") {
        mode = OutputMode::EmitC;
    } else if (modeArgument == "--emit-cpp") {
        mode = OutputMode::EmitCpp;
    } else if (modeArgument == "--emit-ir") {
        mode = OutputMode::EmitIR;
    } else if (modeArgument == "build") {
        mode = OutputMode::Build;
    } else {
        std::cerr << "error: unknown mode '" << modeArgument << "'\n";
        printUsage(std::cerr);
        return 2;
    }

    std::filesystem::path requestedOutput;
    std::vector<std::filesystem::path> linkedFiles;
    std::vector<std::string> libraries;
    BuildOptions options;
    for (int i = 3; i < argc; ++i) {
        const std::string_view option = argv[i];
        if ((mode == OutputMode::Build || mode == OutputMode::EmitCpp) &&
            option == "--cpp-header" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value.empty()) { std::cerr << "error: --cpp-header requires a nonempty value\n"; return 2; }
            options.cppHeaders.emplace_back(value);
            continue;
        }
        if ((mode == OutputMode::Build || mode == OutputMode::EmitIR) &&
            (option == "-O0" || option == "-O1" || option == "-O2" || option == "-O3")) {
            options.optimization = option;
            continue;
        }
        if (mode != OutputMode::Build || i + 1 == argc ||
            (option != "-o" && option != "--link" && option != "-l" && option != "--backend")) {
            printUsage(std::cerr);
            return 2;
        }
        const std::string value = argv[++i];
        if (value.empty()) {
            std::cerr << "error: " << option << " requires a nonempty value\n";
            return 2;
        }
        if (option == "-o") requestedOutput = value;
        else if (option == "--link") linkedFiles.emplace_back(value);
        else if (option == "--backend") {
            if (value != "c" && value != "llvm") {
                std::cerr << "error: backend must be 'c' or 'llvm'\n";
                return 2;
            }
            options.backend = value;
        }
        else libraries.push_back(value);
    }

    const std::string path = argv[2];
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << path << ":1:1: error: could not open source file\n";
        return 1;
    }

    const std::string source{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    basicc::Lexer lexer(source, path);
    basicc::LexResult result = lexer.lex();

    if (mode == OutputMode::Tokens) {
        printTokens(result.tokens);
        printDiagnostics(path, result.diagnostics);
        return result.ok() ? 0 : 1;
    }

    if (!result.ok()) {
        printDiagnostics(path, result.diagnostics);
        return 1;
    }

    basicc::Parser parser(result.tokens);
    basicc::ParseResult parseResult = parser.parse();
    if (!parseResult.ok()) {
        printDiagnostics(path, parseResult.diagnostics);
        return 1;
    }

    if (mode == OutputMode::Ast) {
        basicc::AstPrinter printer;
        std::cout << printer.print(parseResult.program);
        return 0;
    }

    auto modules = basicc::loadModules(std::move(parseResult.program), path);
    if (!modules.ok()) {
        printDiagnostics(path, modules.diagnostics);
        return 1;
    }
    basicc::CompileResult compiled = basicc::compile(modules.program);
    if (!compiled.ok()) {
        printDiagnostics(path, compiled.diagnostics);
        return 1;
    }
    if (mode == OutputMode::EmitC) std::cout << compiled.cSource;
    if (mode == OutputMode::EmitCpp) {
        try { std::cout << cppSource(compiled.cppSource, options); }
        catch (const std::exception& error) { std::cerr << "error: " << error.what() << '\n'; return 1; }
    }
    if (mode == OutputMode::EmitIR) return emitIR(compiled.cSource, options) ? 0 : 1;
    if (mode == OutputMode::Build) {
        std::filesystem::path output = path;
#ifdef _WIN32
        output.replace_extension(".exe");
#else
        output.replace_extension("");
        if (output == std::filesystem::path(path)) output += ".out";
#endif
        if (!requestedOutput.empty()) output = requestedOutput;
        return buildExecutable(compiled, modules.sources, output, linkedFiles, libraries, options) ? 0 : 1;
    }
    return 0;
}
