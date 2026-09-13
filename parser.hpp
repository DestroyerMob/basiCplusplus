#pragma once

#include "ast.hpp"

#include <initializer_list>
#include <memory>
#include <optional>
#include <vector>

namespace basicc {

struct ParseResult {
    Program program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return diagnostics.empty();
    }
};

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);

    ParseResult parse();

private:
    struct ParseError {};

    DeclarationPtr parseTopLevelDeclaration();
    std::unique_ptr<StructDeclaration> parseStruct(Token structToken);
    std::unique_ptr<FunctionDeclaration> parseFunction(Token functionToken, bool external = false,
                                                      bool cpp = false);
    std::unique_ptr<GlobalVariableDeclaration> parseGlobalVariable();
    VariableBinding parseVariableBinding(bool consumeTerminator);
    TypeSyntax parseType();

    StatementPtr parseStatement();
    std::unique_ptr<BlockStatement> parseRequiredBlock();
    StatementPtr parseIfStatement(Token ifToken);
    StatementPtr parseWhileStatement(Token whileToken);
    StatementPtr parseForStatement(Token forToken);
    StatementPtr parseReturnStatement(Token returnToken);
    StatementPtr parseExpressionStatement();

    ExpressionPtr parseExpression();
    ExpressionPtr parseAssignment();
    ExpressionPtr parseConditional();
    ExpressionPtr parseLogicalOr();
    ExpressionPtr parseLogicalAnd();
    ExpressionPtr parseBitwiseOr();
    ExpressionPtr parseBitwiseAnd();
    ExpressionPtr parseSharedComparison();
    ExpressionPtr parseEquality();
    ExpressionPtr parseComparison();
    ExpressionPtr parseTerm();
    ExpressionPtr parseFactor();
    ExpressionPtr parseUnary();
    ExpressionPtr parsePostfix();
    ExpressionPtr parsePrimary();

    [[nodiscard]] bool isVariableDeclarationStart() const;
    [[nodiscard]] bool isTypeStart(TokenType type) const;
    [[nodiscard]] bool isAssignable(const Expression& expression) const;
    [[nodiscard]] bool isStatementStart(TokenType type) const;
    [[nodiscard]] bool isDeclarationStart() const;

    void consumeStatementTerminator();
    void skipNewlines();
    void synchronize(bool insideBlock);
    void report(const Token& token, std::string message);
    [[noreturn]] void fail(const Token& token, std::string message);

    bool match(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, std::string message);
    [[nodiscard]] bool check(TokenType type) const;
    [[nodiscard]] const Token& peek(std::size_t offset = 0) const;
    [[nodiscard]] const Token& previous() const;
    const Token& advance();
    [[nodiscard]] bool atEnd() const;

    const std::vector<Token>& tokens_;
    std::size_t current_ = 0;
    ParseResult result_;
};

} // namespace basicc
