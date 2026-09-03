#pragma once

#include "token.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace basicc {

enum class TypeKind {
    Int,
    Float,
    Bool,
    String,
    Named,
};

struct TypeSyntax {
    TypeKind kind;
    std::string name;
    SourceLocation location;
};

enum class ExpressionKind {
    Literal,
    Identifier,
    Grouping,
    Unary,
    Binary,
    Conditional,
    Assignment,
    Call,
    Index,
    Member,
};

struct Expression {
    Expression(ExpressionKind expressionKind, SourceLocation sourceLocation)
        : kind(expressionKind), location(sourceLocation) {}
    virtual ~Expression() = default;

    ExpressionKind kind;
    SourceLocation location;
};

using ExpressionPtr = std::unique_ptr<Expression>;

struct LiteralExpression final : Expression {
    explicit LiteralExpression(Token literalToken)
        : Expression(ExpressionKind::Literal, literalToken.location),
          token(std::move(literalToken)) {}

    Token token;
};

struct IdentifierExpression final : Expression {
    explicit IdentifierExpression(Token identifier)
        : Expression(ExpressionKind::Identifier, identifier.location),
          name(std::move(identifier.lexeme)) {}

    std::string name;
};

struct GroupingExpression final : Expression {
    GroupingExpression(SourceLocation sourceLocation, ExpressionPtr inner)
        : Expression(ExpressionKind::Grouping, sourceLocation),
          expression(std::move(inner)) {}

    ExpressionPtr expression;
};

struct UnaryExpression final : Expression {
    UnaryExpression(Token operatorToken, ExpressionPtr value)
        : Expression(ExpressionKind::Unary, operatorToken.location),
          operation(std::move(operatorToken)), operand(std::move(value)) {}

    Token operation;
    ExpressionPtr operand;
};

struct BinaryExpression final : Expression {
    BinaryExpression(ExpressionPtr leftValue, Token operatorToken,
                     ExpressionPtr rightValue)
        : Expression(ExpressionKind::Binary, operatorToken.location),
          left(std::move(leftValue)), operation(std::move(operatorToken)),
          right(std::move(rightValue)) {}

    ExpressionPtr left;
    Token operation;
    ExpressionPtr right;
};

struct ConditionalExpression final : Expression {
    ConditionalExpression(SourceLocation sourceLocation, ExpressionPtr conditionValue,
                          ExpressionPtr trueValue, ExpressionPtr falseValue)
        : Expression(ExpressionKind::Conditional, sourceLocation),
          condition(std::move(conditionValue)), whenTrue(std::move(trueValue)),
          whenFalse(std::move(falseValue)) {}

    ExpressionPtr condition;
    ExpressionPtr whenTrue;
    ExpressionPtr whenFalse;
};

struct AssignmentExpression final : Expression {
    AssignmentExpression(ExpressionPtr targetValue, Token operatorToken,
                         ExpressionPtr assignedValue)
        : Expression(ExpressionKind::Assignment, operatorToken.location),
          target(std::move(targetValue)), operation(std::move(operatorToken)),
          value(std::move(assignedValue)) {}

    ExpressionPtr target;
    Token operation;
    ExpressionPtr value;
};

struct CallExpression final : Expression {
    CallExpression(SourceLocation sourceLocation, ExpressionPtr calledValue,
                   std::vector<ExpressionPtr> argumentValues)
        : Expression(ExpressionKind::Call, sourceLocation),
          callee(std::move(calledValue)), arguments(std::move(argumentValues)) {}

    ExpressionPtr callee;
    std::vector<ExpressionPtr> arguments;
};

struct IndexExpression final : Expression {
    IndexExpression(SourceLocation sourceLocation, ExpressionPtr indexedValue,
                    ExpressionPtr indexValue)
        : Expression(ExpressionKind::Index, sourceLocation),
          object(std::move(indexedValue)), index(std::move(indexValue)) {}

    ExpressionPtr object;
    ExpressionPtr index;
};

struct MemberExpression final : Expression {
    MemberExpression(SourceLocation sourceLocation, ExpressionPtr objectValue,
                     std::string memberName)
        : Expression(ExpressionKind::Member, sourceLocation),
          object(std::move(objectValue)), member(std::move(memberName)) {}

    ExpressionPtr object;
    std::string member;
};

struct VariableBinding {
    SourceLocation location;
    std::string name;
    std::optional<TypeSyntax> explicitType;
    ExpressionPtr initializer;
};

enum class StatementKind {
    Block,
    VariableDeclaration,
    Expression,
    Return,
    If,
    While,
    For,
};

struct Statement {
    Statement(StatementKind statementKind, SourceLocation sourceLocation)
        : kind(statementKind), location(sourceLocation) {}
    virtual ~Statement() = default;

    StatementKind kind;
    SourceLocation location;
};

using StatementPtr = std::unique_ptr<Statement>;

struct BlockStatement final : Statement {
    explicit BlockStatement(SourceLocation sourceLocation)
        : Statement(StatementKind::Block, sourceLocation) {}

    std::vector<StatementPtr> statements;
};

struct VariableDeclarationStatement final : Statement {
    explicit VariableDeclarationStatement(VariableBinding variable)
        : Statement(StatementKind::VariableDeclaration, variable.location),
          binding(std::move(variable)) {}

    VariableBinding binding;
};

struct ExpressionStatement final : Statement {
    explicit ExpressionStatement(ExpressionPtr value)
        : Statement(StatementKind::Expression, value->location),
          expression(std::move(value)) {}

    ExpressionPtr expression;
};

struct ReturnStatement final : Statement {
    ReturnStatement(SourceLocation sourceLocation, ExpressionPtr returnedValue)
        : Statement(StatementKind::Return, sourceLocation),
          value(std::move(returnedValue)) {}

    ExpressionPtr value;
};

struct IfStatement final : Statement {
    IfStatement(SourceLocation sourceLocation, ExpressionPtr conditionValue,
                std::unique_ptr<BlockStatement> trueBranch, StatementPtr falseBranch)
        : Statement(StatementKind::If, sourceLocation),
          condition(std::move(conditionValue)), thenBranch(std::move(trueBranch)),
          elseBranch(std::move(falseBranch)) {}

    ExpressionPtr condition;
    std::unique_ptr<BlockStatement> thenBranch;
    StatementPtr elseBranch;
};

struct WhileStatement final : Statement {
    WhileStatement(SourceLocation sourceLocation, ExpressionPtr conditionValue,
                   std::unique_ptr<BlockStatement> loopBody)
        : Statement(StatementKind::While, sourceLocation),
          condition(std::move(conditionValue)), body(std::move(loopBody)) {}

    ExpressionPtr condition;
    std::unique_ptr<BlockStatement> body;
};

struct ForStatement final : Statement {
    ForStatement(SourceLocation sourceLocation, StatementPtr initializerValue,
                 ExpressionPtr conditionValue, ExpressionPtr incrementValue,
                 std::unique_ptr<BlockStatement> loopBody)
        : Statement(StatementKind::For, sourceLocation),
          initializer(std::move(initializerValue)),
          condition(std::move(conditionValue)), increment(std::move(incrementValue)),
          body(std::move(loopBody)) {}

    StatementPtr initializer;
    ExpressionPtr condition;
    ExpressionPtr increment;
    std::unique_ptr<BlockStatement> body;
};

enum class DeclarationKind {
    Function,
    GlobalVariable,
};

struct Declaration {
    Declaration(DeclarationKind declarationKind, SourceLocation sourceLocation)
        : kind(declarationKind), location(sourceLocation) {}
    virtual ~Declaration() = default;

    DeclarationKind kind;
    SourceLocation location;
};

using DeclarationPtr = std::unique_ptr<Declaration>;

struct Parameter {
    SourceLocation location;
    std::string name;
    TypeSyntax type;
};

struct FunctionDeclaration final : Declaration {
    explicit FunctionDeclaration(SourceLocation sourceLocation)
        : Declaration(DeclarationKind::Function, sourceLocation) {}

    std::string name;
    std::vector<Parameter> parameters;
    std::optional<TypeSyntax> returnType;
    std::unique_ptr<BlockStatement> body;
};

struct GlobalVariableDeclaration final : Declaration {
    explicit GlobalVariableDeclaration(VariableBinding variable)
        : Declaration(DeclarationKind::GlobalVariable, variable.location),
          binding(std::move(variable)) {}

    VariableBinding binding;
};

struct Program {
    std::vector<DeclarationPtr> declarations;
};

} // namespace basicc
