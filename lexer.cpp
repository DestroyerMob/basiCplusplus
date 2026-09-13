#include "lexer.hpp"

#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>

namespace basicc {
namespace {

bool isIdentifierStart(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalpha(value) != 0 || character == '_';
}

bool isIdentifierContinue(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) != 0 || character == '_';
}

bool isDigit(char character) {
    return character >= '0' && character <= '9';
}

const std::unordered_map<std::string_view, TokenType> keywords{
    {"fn", TokenType::Fn},             {"func", TokenType::Fn},
    {"function", TokenType::Fn},       {"var", TokenType::Var},
    {"variable", TokenType::Var},      {"int", TokenType::IntType},
    {"integer", TokenType::IntType},   {"float", TokenType::FloatType},
    {"double", TokenType::FloatType},  {"bool", TokenType::BoolType},
    {"boolean", TokenType::BoolType},  {"str", TokenType::StringType},
    {"string", TokenType::StringType}, {"if", TokenType::If},
    {"else", TokenType::Else},         {"while", TokenType::While},
    {"for", TokenType::For},           {"return", TokenType::Return},
    {"import", TokenType::Import},     {"extern", TokenType::Extern},
    {"unsafe", TokenType::Unsafe},     {"struct", TokenType::Struct},
    {"true", TokenType::True},         {"false", TokenType::False},
    {"and", TokenType::And},           {"or", TokenType::Or},
    {"null", TokenType::Null},
};

} // namespace

std::string_view tokenTypeName(TokenType type) {
    switch (type) {
    case TokenType::Identifier: return "Identifier";
    case TokenType::IntegerLiteral: return "IntegerLiteral";
    case TokenType::FloatLiteral: return "FloatLiteral";
    case TokenType::StringLiteral: return "StringLiteral";
    case TokenType::IntType: return "IntType";
    case TokenType::FloatType: return "FloatType";
    case TokenType::BoolType: return "BoolType";
    case TokenType::StringType: return "StringType";
    case TokenType::Plus: return "Plus";
    case TokenType::Minus: return "Minus";
    case TokenType::Star: return "Star";
    case TokenType::Slash: return "Slash";
    case TokenType::Percent: return "Percent";
    case TokenType::Assign: return "Assign";
    case TokenType::EqualEqual: return "EqualEqual";
    case TokenType::Bang: return "Bang";
    case TokenType::BangEqual: return "BangEqual";
    case TokenType::Less: return "Less";
    case TokenType::LessEqual: return "LessEqual";
    case TokenType::Greater: return "Greater";
    case TokenType::GreaterEqual: return "GreaterEqual";
    case TokenType::Ampersand: return "Ampersand";
    case TokenType::AmpersandAmpersand: return "AmpersandAmpersand";
    case TokenType::Pipe: return "Pipe";
    case TokenType::PipePipe: return "PipePipe";
    case TokenType::Question: return "Question";
    case TokenType::Arrow: return "Arrow";
    case TokenType::LeftParen: return "LeftParen";
    case TokenType::RightParen: return "RightParen";
    case TokenType::LeftBracket: return "LeftBracket";
    case TokenType::RightBracket: return "RightBracket";
    case TokenType::Dot: return "Dot";
    case TokenType::Comma: return "Comma";
    case TokenType::Colon: return "Colon";
    case TokenType::Semicolon: return "Semicolon";
    case TokenType::Newline: return "Newline";
    case TokenType::Indent: return "Indent";
    case TokenType::Dedent: return "Dedent";
    case TokenType::EndOfFile: return "EndOfFile";
    case TokenType::Fn: return "Fn";
    case TokenType::Var: return "Var";
    case TokenType::If: return "If";
    case TokenType::Else: return "Else";
    case TokenType::While: return "While";
    case TokenType::For: return "For";
    case TokenType::Return: return "Return";
    case TokenType::Import: return "Import";
    case TokenType::Extern: return "Extern";
    case TokenType::Unsafe: return "Unsafe";
    case TokenType::Struct: return "Struct";
    case TokenType::True: return "True";
    case TokenType::False: return "False";
    case TokenType::Null: return "Null";
    case TokenType::And: return "And";
    case TokenType::Or: return "Or";
    }

    return "Unknown";
}

std::string decodeStringLiteral(const std::string& spelling) {
    std::string value;
    for (std::size_t i = 1; i + 1 < spelling.size(); ++i) {
        char byte = spelling[i];
        if (byte == '\\') {
            // The lexer has already validated the escape. Decode here once so
            // BasicC's \0 followed by a digit never becomes a C octal escape.
            byte = spelling[++i];
            switch (byte) {
            case 'n': byte = '\n'; break;
            case 'r': byte = '\r'; break;
            case 't': byte = '\t'; break;
            case '0': byte = '\0'; break;
            default: break;
            }
        }
        value += byte;
    }
    return value;
}

Lexer::Lexer(std::string_view source, std::string file)
    : source_(source), file_(std::move(file)) {}

LexResult Lexer::lex() {
    if (source_.size() >= 3 &&
        static_cast<unsigned char>(source_[0]) == 0xEF &&
        static_cast<unsigned char>(source_[1]) == 0xBB &&
        static_cast<unsigned char>(source_[2]) == 0xBF) {
        position_ = 3;
    }

    while (!atEnd()) {
        if (atLineStart_) {
            prepareCodeLine();
            if (atEnd() || atLineStart_) {
                continue;
            }
        }

        const char character = peek();
        if (character == ' ') {
            advance();
        } else if (character == '\t') {
            addError(location(), "tabs are not allowed; use spaces");
            advance();
        } else if (character == '\r' || character == '\n') {
            consumeNewline();
        } else if (startsWith("//")) {
            skipLineComment();
        } else if (startsWith("/*")) {
            skipBlockComment();
        } else if (isIdentifierStart(character)) {
            scanIdentifier();
        } else if (isDigit(character)) {
            scanNumber();
        } else if (character == '"') {
            scanString();
        } else {
            scanSymbol();
        }
    }

    while (indentationStack_.size() > 1) {
        indentationStack_.pop_back();
        addSyntheticToken(TokenType::Dedent, location());
    }
    addSyntheticToken(TokenType::EndOfFile, location());

    // Attach the origin once after scanning, including synthetic tokens and
    // diagnostics, so imported AST nodes retain their own file locations.
    if (!file_.empty()) {
        for (auto& token : result_.tokens) token.location.file = file_;
        for (auto& diagnostic : result_.diagnostics) diagnostic.location.file = file_;
    }

    return std::move(result_);
}

bool Lexer::atEnd() const noexcept {
    return position_ >= source_.size();
}

char Lexer::peek(std::size_t offset) const noexcept {
    const std::size_t index = position_ + offset;
    return index < source_.size() ? source_[index] : '\0';
}

bool Lexer::startsWith(std::string_view text) const noexcept {
    return source_.substr(position_, text.size()) == text;
}

SourceLocation Lexer::location() const noexcept {
    return {line_, column_};
}

char Lexer::advance() {
    if (atEnd()) {
        return '\0';
    }

    const char character = source_[position_++];
    if (character == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return character;
}

bool Lexer::consumeIf(char expected) {
    if (peek() != expected) {
        return false;
    }
    advance();
    return true;
}

void Lexer::consumeNewline() {
    const std::size_t start = position_;
    const SourceLocation startLocation = location();
    const bool lineHadCode = !atLineStart_;

    if (peek() == '\r') {
        advance();
        if (peek() == '\n') {
            advance();
        } else {
            ++line_;
            column_ = 1;
        }
    } else {
        advance();
    }

    if (lineHadCode) {
        addToken(TokenType::Newline, start, startLocation);
    }

    atLineStart_ = true;
    leadingCommentSeen_ = false;
    pendingIndentation_ = 0;
}

void Lexer::prepareCodeLine() {
    while (!atEnd() && atLineStart_) {
        if (peek() == ' ') {
            if (!leadingCommentSeen_) {
                ++pendingIndentation_;
            }
            advance();
            continue;
        }

        if (peek() == '\t') {
            addError(location(), "tabs are not allowed; use spaces");
            if (!leadingCommentSeen_) {
                pendingIndentation_ += 4 - (pendingIndentation_ % 4);
            }
            advance();
            continue;
        }

        if (peek() == '\r' || peek() == '\n') {
            consumeNewline();
            continue;
        }

        if (startsWith("//")) {
            leadingCommentSeen_ = true;
            skipLineComment();
            continue;
        }

        if (startsWith("/*")) {
            leadingCommentSeen_ = true;
            skipBlockComment();
            continue;
        }

        applyIndentation(pendingIndentation_);
        atLineStart_ = false;
    }
}

void Lexer::applyIndentation(std::size_t indentation) {
    const SourceLocation indentLocation{line_, 1};
    if (indentation % 4 != 0) {
        addError(indentLocation, "indentation must use groups of four spaces");
    }

    const std::size_t current = indentationStack_.back();
    if (indentation > current) {
        if (indentation != current + 4) {
            addError(indentLocation,
                     "an indentation level must increase by exactly four spaces");
        }
        indentationStack_.push_back(indentation);
        addSyntheticToken(TokenType::Indent, indentLocation);
        return;
    }

    while (indentation < indentationStack_.back() &&
           indentationStack_.size() > 1) {
        indentationStack_.pop_back();
        addSyntheticToken(TokenType::Dedent, indentLocation);
    }

    if (indentation != indentationStack_.back()) {
        addError(indentLocation,
                 "indentation does not match an earlier indentation level");
    }
}

void Lexer::skipLineComment() {
    advance();
    advance();
    while (!atEnd() && peek() != '\r' && peek() != '\n') {
        advance();
    }
}

void Lexer::skipBlockComment() {
    const SourceLocation startLocation = location();
    advance();
    advance();

    while (!atEnd()) {
        if (startsWith("*/")) {
            advance();
            advance();
            return;
        }
        if (peek() == '\r' || peek() == '\n') {
            consumeNewline();
        } else {
            advance();
        }
    }

    addError(startLocation, "unterminated block comment");
}

void Lexer::scanIdentifier() {
    const std::size_t start = position_;
    const SourceLocation startLocation = location();
    advance();
    while (isIdentifierContinue(peek())) {
        advance();
    }

    const std::string_view text = source_.substr(start, position_ - start);
    const auto found = keywords.find(text);
    addToken(found == keywords.end() ? TokenType::Identifier : found->second,
             start, startLocation);
}

void Lexer::scanNumber() {
    const std::size_t start = position_;
    const SourceLocation startLocation = location();
    bool isFloat = false;
    bool malformed = false;

    while (isDigit(peek())) {
        advance();
    }

    if (peek() == '.' && isDigit(peek(1))) {
        isFloat = true;
        advance();
        while (isDigit(peek())) {
            advance();
        }
    }

    if (peek() == 'e' || peek() == 'E') {
        isFloat = true;
        advance();
        if (peek() == '+' || peek() == '-') {
            advance();
        }
        if (!isDigit(peek())) {
            malformed = true;
        }
        while (isDigit(peek())) {
            advance();
        }
    }

    if (isIdentifierStart(peek()) ||
        (peek() == '.' && isDigit(peek(1)))) {
        malformed = true;
        while (isIdentifierContinue(peek()) || peek() == '.') {
            advance();
        }
    }

    if (malformed) {
        addError(startLocation, "malformed numeric literal");
    }
    addToken(isFloat ? TokenType::FloatLiteral : TokenType::IntegerLiteral,
             start, startLocation);
}

void Lexer::scanString() {
    const std::size_t start = position_;
    const SourceLocation startLocation = location();
    advance();
    bool terminated = false;

    while (!atEnd() && peek() != '\r' && peek() != '\n') {
        if (peek() == '"') {
            advance();
            terminated = true;
            break;
        }

        if (peek() == '\\') {
            const SourceLocation escapeLocation = location();
            advance();
            if (atEnd() || peek() == '\r' || peek() == '\n') {
                break;
            }
            const char escaped = advance();
            if (escaped != '"' && escaped != '\\' && escaped != 'n' &&
                escaped != 'r' && escaped != 't' && escaped != '0') {
                addError(escapeLocation, "unsupported string escape sequence");
            }
        } else {
            advance();
        }
    }

    if (!terminated) {
        addError(startLocation, "unterminated string literal");
    }
    addToken(TokenType::StringLiteral, start, startLocation);
}

void Lexer::scanSymbol() {
    const std::size_t start = position_;
    const SourceLocation startLocation = location();
    const char character = advance();

    switch (character) {
    case '+': addToken(TokenType::Plus, start, startLocation); return;
    case '-':
        addToken(consumeIf('>') ? TokenType::Arrow : TokenType::Minus, start,
                 startLocation);
        return;
    case '*': addToken(TokenType::Star, start, startLocation); return;
    case '/': addToken(TokenType::Slash, start, startLocation); return;
    case '%': addToken(TokenType::Percent, start, startLocation); return;
    case '=':
        addToken(consumeIf('=') ? TokenType::EqualEqual : TokenType::Assign,
                 start, startLocation);
        return;
    case '!':
        addToken(consumeIf('=') ? TokenType::BangEqual : TokenType::Bang,
                 start, startLocation);
        return;
    case '<':
        addToken(consumeIf('=') ? TokenType::LessEqual : TokenType::Less,
                 start, startLocation);
        return;
    case '>':
        addToken(consumeIf('=') ? TokenType::GreaterEqual : TokenType::Greater,
                 start, startLocation);
        return;
    case '&':
        addToken(consumeIf('&') ? TokenType::AmpersandAmpersand
                                : TokenType::Ampersand,
                 start, startLocation);
        return;
    case '|':
        addToken(consumeIf('|') ? TokenType::PipePipe : TokenType::Pipe, start,
                 startLocation);
        return;
    case '?': addToken(TokenType::Question, start, startLocation); return;
    case '(': addToken(TokenType::LeftParen, start, startLocation); return;
    case ')': addToken(TokenType::RightParen, start, startLocation); return;
    case '[': addToken(TokenType::LeftBracket, start, startLocation); return;
    case ']': addToken(TokenType::RightBracket, start, startLocation); return;
    case '.': addToken(TokenType::Dot, start, startLocation); return;
    case ',': addToken(TokenType::Comma, start, startLocation); return;
    case ':': addToken(TokenType::Colon, start, startLocation); return;
    case ';': addToken(TokenType::Semicolon, start, startLocation); return;
    default:
        addError(startLocation,
                 std::string("unknown character '") + character + "'");
        return;
    }
}

void Lexer::addToken(TokenType type, std::size_t start,
                     SourceLocation startLocation) {
    result_.tokens.push_back(
        {type, std::string(source_.substr(start, position_ - start)),
         startLocation});
}

void Lexer::addSyntheticToken(TokenType type, SourceLocation tokenLocation) {
    result_.tokens.push_back({type, {}, tokenLocation});
}

void Lexer::addError(SourceLocation errorLocation, std::string message) {
    result_.diagnostics.push_back({errorLocation, std::move(message)});
}

} // namespace basicc
