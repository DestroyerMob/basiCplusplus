#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace basicc {

enum class TokenType {
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,

    IntType,
    FloatType,
    BoolType,
    StringType,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Assign,
    EqualEqual,
    Bang,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Ampersand,
    AmpersandAmpersand,
    Pipe,
    PipePipe,
    Question,
    Arrow,

    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    Dot,
    Comma,
    Colon,
    Semicolon,

    Newline,
    Indent,
    Dedent,
    EndOfFile,

    Fn,
    Var,
    If,
    Else,
    While,
    For,
    Return,
    Import,
    Extern,
    Unsafe,
    Struct,
    True,
    False,
};

struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

struct Token {
    TokenType type;
    std::string lexeme;
    SourceLocation location;
};

struct Diagnostic {
    SourceLocation location;
    std::string message;
};

std::string_view tokenTypeName(TokenType type);

} // namespace basicc
