#include "ast_printer.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class OutputMode {
    Tokens,
    Ast,
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
    output << "usage: basicc (--tokens|--ast) <file.bc>\n";
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
        std::cerr << path << ':' << diagnostic.location.line << ':'
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
    if (argc != 3) {
        printUsage(std::cerr);
        return 2;
    }

    OutputMode mode;
    const std::string_view modeArgument = argv[1];
    if (modeArgument == "--tokens") {
        mode = OutputMode::Tokens;
    } else if (modeArgument == "--ast") {
        mode = OutputMode::Ast;
    } else {
        std::cerr << "error: unknown mode '" << modeArgument << "'\n";
        printUsage(std::cerr);
        return 2;
    }

    const std::string path = argv[2];
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << path << ":1:1: error: could not open source file\n";
        return 1;
    }

    const std::string source{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    basicc::Lexer lexer(source);
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

    basicc::AstPrinter printer;
    std::cout << printer.print(parseResult.program);
    return 0;
}
