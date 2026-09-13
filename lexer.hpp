#pragma once

#include "token.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace basicc {

struct LexResult {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return diagnostics.empty();
    }
};

class Lexer {
public:
    explicit Lexer(std::string_view source, std::string file = {});

    LexResult lex();

private:
    [[nodiscard]] bool atEnd() const noexcept;
    [[nodiscard]] char peek(std::size_t offset = 0) const noexcept;
    [[nodiscard]] bool startsWith(std::string_view text) const noexcept;
    [[nodiscard]] SourceLocation location() const noexcept;

    char advance();
    bool consumeIf(char expected);
    void consumeNewline();
    void prepareCodeLine();
    void applyIndentation(std::size_t indentation);
    void skipLineComment();
    void skipBlockComment();
    void scanIdentifier();
    void scanNumber();
    void scanString();
    void scanSymbol();

    void addToken(TokenType type, std::size_t start,
                  SourceLocation startLocation);
    void addSyntheticToken(TokenType type, SourceLocation tokenLocation);
    void addError(SourceLocation errorLocation, std::string message);

    std::string_view source_;
    std::string file_;
    std::size_t position_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
    bool atLineStart_ = true;
    bool leadingCommentSeen_ = false;
    std::size_t pendingIndentation_ = 0;
    std::vector<std::size_t> indentationStack_{0};
    LexResult result_;
};

} // namespace basicc
