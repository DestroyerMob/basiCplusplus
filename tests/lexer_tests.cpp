#include "../lexer.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using basicc::LexResult;
using basicc::Lexer;
using basicc::TokenType;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<TokenType> typesOf(const LexResult& result) {
    std::vector<TokenType> types;
    for (const basicc::Token& token : result.tokens) {
        types.push_back(token.type);
    }
    return types;
}

bool hasDiagnostic(const LexResult& result, std::string_view text) {
    for (const basicc::Diagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void testKeywordsAndIdentifiers() {
    Lexer lexer("fn func function var variable int integer float double bool "
                "boolean str string if else while for return import extern "
                "unsafe struct true false name name2 _private\n");
    const LexResult result = lexer.lex();
    check(result.ok(), "keywords, aliases, and identifiers should be valid");

    const std::vector<TokenType> expected{
        TokenType::Fn, TokenType::Fn, TokenType::Fn,
        TokenType::Var, TokenType::Var,
        TokenType::IntType, TokenType::IntType,
        TokenType::FloatType, TokenType::FloatType,
        TokenType::BoolType, TokenType::BoolType,
        TokenType::StringType, TokenType::StringType,
        TokenType::If, TokenType::Else, TokenType::While, TokenType::For,
        TokenType::Return, TokenType::Import, TokenType::Extern,
        TokenType::Unsafe, TokenType::Struct, TokenType::True,
        TokenType::False, TokenType::Identifier, TokenType::Identifier,
        TokenType::Identifier, TokenType::Newline, TokenType::EndOfFile,
    };
    check(typesOf(result) == expected,
          "aliases should resolve to their canonical token types");
    check(result.tokens[1].lexeme == "func" &&
              result.tokens[2].lexeme == "function",
          "alias tokens should preserve their original spelling");
}

void testLiteralsAndSymbols() {
    Lexer lexer(
        "12 3.5 1e6 2.5e-3 \"a\\n\\\"b\" "
        "+ - * / % = == ! != < <= > >= & && | || ? -> "
        "( ) [ ] . , : ;\n");
    const LexResult result = lexer.lex();
    check(result.ok(), "valid literals and symbols should lex without errors");

    const std::vector<TokenType> types = typesOf(result);
    const std::vector<TokenType> expected{
        TokenType::IntegerLiteral, TokenType::FloatLiteral,
        TokenType::FloatLiteral, TokenType::FloatLiteral,
        TokenType::StringLiteral, TokenType::Plus, TokenType::Minus,
        TokenType::Star, TokenType::Slash, TokenType::Percent,
        TokenType::Assign, TokenType::EqualEqual, TokenType::Bang,
        TokenType::BangEqual, TokenType::Less, TokenType::LessEqual,
        TokenType::Greater, TokenType::GreaterEqual, TokenType::Ampersand,
        TokenType::AmpersandAmpersand, TokenType::Pipe, TokenType::PipePipe,
        TokenType::Question, TokenType::Arrow, TokenType::LeftParen,
        TokenType::RightParen, TokenType::LeftBracket, TokenType::RightBracket,
        TokenType::Dot, TokenType::Comma, TokenType::Colon,
        TokenType::Semicolon, TokenType::Newline, TokenType::EndOfFile,
    };
    check(types == expected,
          "all documented literals and symbols should have stable token types");
}

void testIndentationAndComments() {
    Lexer lexer(
        "if true:\r\n"
        "    // comment-only line\r\n"
        "    /* block-comment-only\r\n"
        "       line */\r\n"
        "    while false:\r\n"
        "        value = 1; /* inline */\r\n"
        "\r\n"
        "    value = 2;\r\n"
        "value = 3;\r\n");
    const LexResult result = lexer.lex();
    check(result.ok(), "nested indentation and comments should be valid");

    int indents = 0;
    int dedents = 0;
    for (const basicc::Token& token : result.tokens) {
        indents += token.type == TokenType::Indent ? 1 : 0;
        dedents += token.type == TokenType::Dedent ? 1 : 0;
    }
    check(indents == 2, "nested blocks should emit two indent tokens");
    check(dedents == 2, "nested blocks should emit two dedent tokens");
}

void testBom() {
    const std::string source = "\xEF\xBB\xBFvar value = 1;\n";
    Lexer lexer(source);
    const LexResult result = lexer.lex();
    check(result.ok(), "a UTF-8 BOM should be accepted");
    check(result.tokens.front().type == TokenType::Var,
          "the BOM should not become a token");
    check(result.tokens.front().location.line == 1 &&
              result.tokens.front().location.column == 1,
          "the BOM should not change source columns");
}

void testDiagnostics() {
    {
        Lexer lexer("if true:\n\tvalue = 1;\n");
        const LexResult result = lexer.lex();
        check(hasDiagnostic(result, "tabs are not allowed"),
              "tabs should be diagnosed");
    }
    {
        Lexer lexer("if true:\n    value = 1;\n  value = 2;\n");
        const LexResult result = lexer.lex();
        check(hasDiagnostic(result, "groups of four spaces"),
              "partial indentation should be diagnosed");
        check(hasDiagnostic(result, "does not match"),
              "an unmatched dedent should be diagnosed");
    }
    {
        Lexer lexer("var value = 1.2.3; @\n");
        const LexResult result = lexer.lex();
        check(hasDiagnostic(result, "malformed numeric literal"),
              "malformed numbers should be diagnosed");
        check(hasDiagnostic(result, "unknown character"),
              "unknown characters should be diagnosed");
    }
    {
        Lexer lexer("var text = \"unfinished\n");
        check(hasDiagnostic(lexer.lex(), "unterminated string literal"),
              "unterminated strings should be diagnosed");
    }
    {
        Lexer lexer("var text = \"bad\\q\";\n");
        check(hasDiagnostic(lexer.lex(), "unsupported string escape"),
              "unsupported string escapes should be diagnosed");
    }
    {
        Lexer lexer("/* unfinished");
        check(hasDiagnostic(lexer.lex(), "unterminated block comment"),
              "unterminated block comments should be diagnosed");
    }
}

} // namespace

int main() {
    testKeywordsAndIdentifiers();
    testLiteralsAndSymbols();
    testIndentationAndComments();
    testBom();
    testDiagnostics();

    if (failures != 0) {
        std::cerr << failures << " lexer test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All lexer tests passed\n";
    return EXIT_SUCCESS;
}
