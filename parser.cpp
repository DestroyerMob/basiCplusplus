#include "parser.hpp"

#include <string>
#include <utility>

namespace basicc {
namespace {

bool isComparisonOperator(TokenType type) {
    return type == TokenType::EqualEqual || type == TokenType::BangEqual ||
           type == TokenType::Less || type == TokenType::LessEqual ||
           type == TokenType::Greater || type == TokenType::GreaterEqual;
}

} // namespace

Parser::Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}

ParseResult Parser::parse() {
    skipNewlines();
    while (!atEnd()) {
        try {
            DeclarationPtr declaration = parseTopLevelDeclaration();
            if (declaration) {
                result_.program.declarations.push_back(std::move(declaration));
            }
        } catch (const ParseError&) {
            synchronize(false);
        }
        skipNewlines();
    }
    return std::move(result_);
}

DeclarationPtr Parser::parseTopLevelDeclaration() {
    if (match({TokenType::Import})) {
        const auto location = previous().location;
        const Token path = consume(TokenType::StringLiteral, "expected a quoted import path");
        consumeStatementTerminator();
        return std::make_unique<ImportDeclaration>(location, path);
    }
    if (match({TokenType::Struct})) return parseStruct(previous());
    if (match({TokenType::Extern})) {
        const Token external = previous();
        bool cpp = false;
        if (match({TokenType::StringLiteral})) {
            const auto language = decodeStringLiteral(previous().lexeme);
            if (language != "C" && language != "C++") fail(previous(), "extern language must be C or C++");
            cpp = language == "C++";
        }
        consume(TokenType::Fn, "expected 'fn' after 'extern'");
        return parseFunction(external, true, cpp);
    }
    if (match({TokenType::Fn})) {
        return parseFunction(previous());
    }
    if (isVariableDeclarationStart()) {
        return parseGlobalVariable();
    }

    if (match({TokenType::Unsafe})) {
        fail(previous(), "unsafe blocks are allowed only inside functions");
    }

    fail(peek(), "expected a function, struct, or global variable declaration");
}

std::unique_ptr<StructDeclaration> Parser::parseStruct(Token structToken) {
    auto structure = std::make_unique<StructDeclaration>(structToken.location);
    structure->name = consume(TokenType::Identifier, "expected a struct name").lexeme;
    consume(TokenType::Colon, "expected ':' after the struct name");
    consume(TokenType::Newline, "expected a newline after ':'");
    consume(TokenType::Indent, "expected indented struct fields");
    skipNewlines();
    while (!check(TokenType::Dedent) && !atEnd()) {
        if (check(TokenType::Var)) {
            fail(peek(), "inferred struct fields are reserved but not supported yet; use 'name: type;'");
        }
        const Token name = consume(TokenType::Identifier, "expected a field name");
        consume(TokenType::Colon, "expected ':' after the field name");
        structure->fields.push_back({name.location, name.lexeme, parseType()});
        consumeStatementTerminator();
    }
    consume(TokenType::Dedent, "expected the end of the struct fields");
    if (structure->fields.empty()) fail(structToken, "a struct needs at least one field");
    return structure;
}

std::unique_ptr<FunctionDeclaration> Parser::parseFunction(Token functionToken, bool external, bool cpp) {
    const Token name = consume(TokenType::Identifier,
                               "expected a function name after 'fn'");
    auto function =
        std::make_unique<FunctionDeclaration>(functionToken.location);
    function->name = name.lexeme;
    function->external = external;

    consume(TokenType::LeftParen, "expected '(' after the function name");
    if (!check(TokenType::RightParen)) {
        do {
            const Token parameterName =
                consume(TokenType::Identifier, "expected a parameter name");
            consume(TokenType::Colon, "expected ':' after the parameter name");
            TypeSyntax parameterType = parseType();
            function->parameters.push_back(
                {parameterName.location, parameterName.lexeme,
                 std::move(parameterType)});
        } while (match({TokenType::Comma}) && !check(TokenType::RightParen));
    }
    consume(TokenType::RightParen, "expected ')' after the parameters");

    if (match({TokenType::Arrow})) {
        function->returnType = parseType();
    }
    if (external) {
        if (!function->returnType) fail(peek(), "extern functions require an explicit return type");
        if (cpp) {
            function->cppTarget = name.lexeme;
            if (match({TokenType::Assign})) {
                const Token target = consume(TokenType::StringLiteral, "expected a quoted C++ function name");
                function->cppTarget = decodeStringLiteral(target.lexeme);
                // Only qualified identifiers enter generated C++; never splice
                // an arbitrary source expression from the quoted binding.
                const auto& text = function->cppTarget;
                std::size_t i = text.rfind("::", 0) == 0 ? 2 : 0;
                const auto letter = [](char c) {
                    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
                };
                for (;;) {
                    if (i == text.size() || !letter(text[i])) fail(target, "expected a qualified C++ identifier");
                    ++i;
                    while (i < text.size() && (letter(text[i]) || (text[i] >= '0' && text[i] <= '9'))) ++i;
                    if (i == text.size()) break;
                    if (text.compare(i, 2, "::") != 0) fail(target, "expected a qualified C++ identifier");
                    i += 2;
                }
            }
        }
        consumeStatementTerminator();
    } else {
        function->body = parseRequiredBlock();
    }
    return function;
}

std::unique_ptr<GlobalVariableDeclaration> Parser::parseGlobalVariable() {
    return std::make_unique<GlobalVariableDeclaration>(
        parseVariableBinding(true));
}

VariableBinding Parser::parseVariableBinding(bool consumeTerminator) {
    const SourceLocation declarationLocation = peek().location;
    std::optional<TypeSyntax> explicitType;
    Token name = peek();

    if (match({TokenType::Var})) {
        name = consume(TokenType::Identifier,
                       "expected a variable name after 'var'");
        if (match({TokenType::Colon})) {
            explicitType = parseType();
        }
    } else {
        explicitType = parseType();
        name = consume(TokenType::Identifier,
                       "expected a variable name after its type");
    }

    consume(TokenType::Assign,
            "variables must have an initializer introduced by '='");
    ExpressionPtr initializer = parseExpression();
    if (consumeTerminator) {
        consumeStatementTerminator();
    }

    return {declarationLocation, name.lexeme, std::move(explicitType),
            std::move(initializer)};
}

TypeSyntax Parser::parseType() {
    const Token typeToken = peek();
    TypeSyntax type{TypeKind::Named, typeToken.lexeme, typeToken.location};
    if (match({TokenType::IntType})) {
        type.kind = TypeKind::Int;
        type.name = "int";
    } else if (match({TokenType::FloatType})) {
        type.kind = TypeKind::Float;
        type.name = "float";
    } else if (match({TokenType::BoolType})) {
        type.kind = TypeKind::Bool;
        type.name = "bool";
    } else if (match({TokenType::StringType})) {
        type.kind = TypeKind::String;
        type.name = "string";
    } else if (!match({TokenType::Identifier})) {
        fail(typeToken, "expected a type name");
    }
    while (check(TokenType::LeftBracket) || check(TokenType::Star)) {
        if (match({TokenType::Star})) type.suffixes.push_back(TypeSuffix::Pointer);
        else {
            advance();
            consume(TokenType::RightBracket, "expected ']' in an array type");
            type.suffixes.push_back(TypeSuffix::Array);
        }
    }
    return type;
}

StatementPtr Parser::parseStatement() {
    if (match({TokenType::Unsafe})) {
        const auto location = previous().location;
        return std::make_unique<UnsafeStatement>(location, parseRequiredBlock());
    }
    if (isVariableDeclarationStart()) {
        return std::make_unique<VariableDeclarationStatement>(
            parseVariableBinding(true));
    }
    if (match({TokenType::Return})) {
        return parseReturnStatement(previous());
    }
    if (match({TokenType::If})) {
        return parseIfStatement(previous());
    }
    if (match({TokenType::While})) {
        return parseWhileStatement(previous());
    }
    if (match({TokenType::For})) {
        return parseForStatement(previous());
    }
    if (match({TokenType::Import, TokenType::Extern,
               TokenType::Struct, TokenType::Fn})) {
        fail(previous(), std::string(tokenTypeName(previous().type)) +
                             " syntax is not supported in this block");
    }
    return parseExpressionStatement();
}

std::unique_ptr<BlockStatement> Parser::parseRequiredBlock() {
    const Token colon = consume(TokenType::Colon,
                                "expected ':' before the indented block");
    consume(TokenType::Newline, "expected a newline after ':'");
    consume(TokenType::Indent, "expected an indented block");

    auto block = std::make_unique<BlockStatement>(colon.location);
    skipNewlines();
    while (!check(TokenType::Dedent) && !atEnd()) {
        try {
            StatementPtr statement = parseStatement();
            if (statement) {
                block->statements.push_back(std::move(statement));
            }
        } catch (const ParseError&) {
            synchronize(true);
        }
        skipNewlines();
    }

    consume(TokenType::Dedent, "expected the end of the indented block");
    if (block->statements.empty()) {
        report(colon, "an indented block must contain at least one statement");
    }
    return block;
}

StatementPtr Parser::parseIfStatement(Token ifToken) {
    ExpressionPtr condition = parseExpression();
    std::unique_ptr<BlockStatement> thenBranch = parseRequiredBlock();
    StatementPtr elseBranch;

    if (match({TokenType::Else})) {
        if (match({TokenType::If})) {
            elseBranch = parseIfStatement(previous());
        } else {
            elseBranch = parseRequiredBlock();
        }
    }

    return std::make_unique<IfStatement>(
        ifToken.location, std::move(condition), std::move(thenBranch),
        std::move(elseBranch));
}

StatementPtr Parser::parseWhileStatement(Token whileToken) {
    ExpressionPtr condition = parseExpression();
    std::unique_ptr<BlockStatement> body = parseRequiredBlock();
    return std::make_unique<WhileStatement>(
        whileToken.location, std::move(condition), std::move(body));
}

StatementPtr Parser::parseForStatement(Token forToken) {
    consume(TokenType::LeftParen, "expected '(' after 'for'");

    StatementPtr initializer;
    if (match({TokenType::Semicolon})) {
        // Empty initializer.
    } else if (isVariableDeclarationStart()) {
        initializer = std::make_unique<VariableDeclarationStatement>(
            parseVariableBinding(true));
    } else {
        ExpressionPtr value = parseExpression();
        consume(TokenType::Semicolon,
                "expected ';' after the for-loop initializer");
        initializer = std::make_unique<ExpressionStatement>(std::move(value));
    }

    ExpressionPtr condition;
    if (!check(TokenType::Semicolon)) {
        condition = parseExpression();
    }
    consume(TokenType::Semicolon, "expected ';' after the for-loop condition");

    ExpressionPtr increment;
    if (!check(TokenType::RightParen)) {
        increment = parseExpression();
    }
    consume(TokenType::RightParen, "expected ')' after the for-loop clauses");

    std::unique_ptr<BlockStatement> body = parseRequiredBlock();
    return std::make_unique<ForStatement>(
        forToken.location, std::move(initializer), std::move(condition),
        std::move(increment), std::move(body));
}

StatementPtr Parser::parseReturnStatement(Token returnToken) {
    ExpressionPtr value;
    if (!check(TokenType::Semicolon)) {
        value = parseExpression();
    }
    consumeStatementTerminator();
    return std::make_unique<ReturnStatement>(returnToken.location,
                                             std::move(value));
}

StatementPtr Parser::parseExpressionStatement() {
    ExpressionPtr expression = parseExpression();
    consumeStatementTerminator();
    return std::make_unique<ExpressionStatement>(std::move(expression));
}

ExpressionPtr Parser::parseExpression() {
    return parseAssignment();
}

ExpressionPtr Parser::parseAssignment() {
    ExpressionPtr expression = parseConditional();
    if (match({TokenType::Assign})) {
        const Token operation = previous();
        ExpressionPtr value = parseAssignment();
        if (!isAssignable(*expression)) {
            report(operation,
                   "the left side of an assignment is not assignable");
        }
        return std::make_unique<AssignmentExpression>(
            std::move(expression), operation, std::move(value));
    }
    return expression;
}

ExpressionPtr Parser::parseConditional() {
    ExpressionPtr condition = parseLogicalOr();
    if (!match({TokenType::Question})) {
        return condition;
    }

    const Token question = previous();
    ExpressionPtr whenTrue = parseExpression();
    consume(TokenType::Colon, "expected ':' in the conditional expression");
    ExpressionPtr whenFalse = parseAssignment();
    return std::make_unique<ConditionalExpression>(
        question.location, std::move(condition), std::move(whenTrue),
        std::move(whenFalse));
}

ExpressionPtr Parser::parseLogicalOr() {
    ExpressionPtr expression = parseLogicalAnd();
    while (match({TokenType::PipePipe})) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            std::move(expression), operation, parseLogicalAnd());
    }
    return expression;
}

ExpressionPtr Parser::parseLogicalAnd() {
    ExpressionPtr expression = parseBitwiseOr();
    while (match({TokenType::AmpersandAmpersand})) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            std::move(expression), operation, parseBitwiseOr());
    }
    return expression;
}

ExpressionPtr Parser::parseBitwiseOr() {
    ExpressionPtr expression = parseBitwiseAnd();
    while (match({TokenType::Pipe})) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            std::move(expression), operation, parseBitwiseAnd());
    }
    return expression;
}

ExpressionPtr Parser::parseBitwiseAnd() {
    ExpressionPtr expression = parseSharedComparison();
    while (match({TokenType::Ampersand})) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            std::move(expression), operation, parseSharedComparison());
    }
    return expression;
}

ExpressionPtr Parser::parseSharedComparison() {
    ExpressionPtr first = parseEquality();
    if (!match({TokenType::And, TokenType::Or})) {
        return first;
    }

    const Token grouping = previous();
    if (first->kind == ExpressionKind::Binary &&
        isComparisonOperator(
            static_cast<const BinaryExpression&>(*first).operation.type)) {
        fail(grouping,
             "'and' and 'or' group values before one shared comparison; "
             "use '&&' or '||' to combine conditions");
    }

    std::vector<ExpressionPtr> operands;
    operands.push_back(std::move(first));
    operands.push_back(parseTerm());
    while (match({TokenType::And, TokenType::Or})) {
        if (previous().type != grouping.type) {
            fail(previous(), "cannot mix 'and' and 'or' in one shared comparison; "
                             "combine separate comparisons with '&&' or '||'");
        }
        operands.push_back(parseTerm());
    }

    if (!isComparisonOperator(peek().type)) {
        fail(peek(), "expected '==', '!=', '<', '<=', '>', or '>=' "
                     "after the values in a shared comparison");
    }
    const Token operation = advance();
    ExpressionPtr state = parseTerm();
    if (isComparisonOperator(peek().type) || check(TokenType::And) ||
        check(TokenType::Or)) {
        fail(peek(), "a shared comparison has one operator and one target; "
                     "use '&&' or '||' to combine conditions");
    }

    return std::make_unique<SharedComparisonExpression>(
        std::move(operands), grouping, operation, std::move(state));
}

ExpressionPtr Parser::parseEquality() {
    ExpressionPtr expression = parseComparison();
    while (match({TokenType::EqualEqual, TokenType::BangEqual})) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            std::move(expression), operation, parseComparison());
    }
    return expression;
}

ExpressionPtr Parser::parseComparison() {
    ExpressionPtr expression = parseTerm();
    while (match({TokenType::Less, TokenType::LessEqual, TokenType::Greater,
                  TokenType::GreaterEqual})) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            std::move(expression), operation, parseTerm());
    }
    return expression;
}

ExpressionPtr Parser::parseTerm() {
    ExpressionPtr expression = parseFactor();
    while (match({TokenType::Plus, TokenType::Minus})) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            std::move(expression), operation, parseFactor());
    }
    return expression;
}

ExpressionPtr Parser::parseFactor() {
    ExpressionPtr expression = parseUnary();
    while (match({TokenType::Star, TokenType::Slash, TokenType::Percent})) {
        const Token operation = previous();
        expression = std::make_unique<BinaryExpression>(
            std::move(expression), operation, parseUnary());
    }
    return expression;
}

ExpressionPtr Parser::parseUnary() {
    if (match({TokenType::Bang, TokenType::Plus, TokenType::Minus,
               TokenType::Star, TokenType::Ampersand})) {
        const Token operation = previous();
        return std::make_unique<UnaryExpression>(operation, parseUnary());
    }
    return parsePostfix();
}

ExpressionPtr Parser::parsePostfix() {
    ExpressionPtr expression = parsePrimary();

    while (true) {
        if (match({TokenType::LeftParen})) {
            const SourceLocation callLocation = previous().location;
            std::vector<ExpressionPtr> arguments;
            if (!check(TokenType::RightParen)) {
                do {
                    arguments.push_back(parseExpression());
                } while (match({TokenType::Comma}) &&
                         !check(TokenType::RightParen));
            }
            consume(TokenType::RightParen, "expected ')' after the arguments");
            expression = std::make_unique<CallExpression>(
                callLocation, std::move(expression), std::move(arguments));
        } else if (match({TokenType::LeftBracket})) {
            const SourceLocation indexLocation = previous().location;
            ExpressionPtr index = parseExpression();
            consume(TokenType::RightBracket, "expected ']' after the index");
            expression = std::make_unique<IndexExpression>(
                indexLocation, std::move(expression), std::move(index));
        } else if (match({TokenType::Dot})) {
            const SourceLocation memberLocation = previous().location;
            const Token member =
                consume(TokenType::Identifier, "expected a member name after '.'");
            expression = std::make_unique<MemberExpression>(
                memberLocation, std::move(expression), member.lexeme);
        } else {
            break;
        }
    }

    return expression;
}

ExpressionPtr Parser::parsePrimary() {
    if (match({TokenType::LeftBracket})) {
        auto array = std::make_unique<ArrayExpression>(previous().location);
        if (!check(TokenType::RightBracket)) {
            do {
                array->elements.push_back(parseExpression());
            } while (match({TokenType::Comma}) && !check(TokenType::RightBracket));
        }
        consume(TokenType::RightBracket, "expected ']' after the array elements");
        return array;
    }
    if (match({TokenType::IntegerLiteral, TokenType::FloatLiteral,
               TokenType::StringLiteral, TokenType::True, TokenType::False, TokenType::Null})) {
        return std::make_unique<LiteralExpression>(previous());
    }
    if (match({TokenType::Identifier})) {
        if (previous().lexeme == "cast") {
            const auto location = previous().location;
            consume(TokenType::Less, "expected '<type>' after 'cast'");
            auto target = parseType();
            consume(TokenType::Greater, "expected '>' after the cast type");
            consume(TokenType::LeftParen, "expected '(' before the cast value");
            auto value = parseExpression();
            consume(TokenType::RightParen, "expected ')' after the cast value");
            return std::make_unique<CastExpression>(location, std::move(target), std::move(value));
        }
        return std::make_unique<IdentifierExpression>(previous());
    }
    if (match({TokenType::IntType, TokenType::FloatType, TokenType::StringType})) {
        // Type names in expressions denote explicit conversions. Keep the
        // original alias spelling; ordinary postfix parsing handles the call.
        const Token conversion = previous();
        if (!check(TokenType::LeftParen)) {
            fail(peek(), "expected '(' after the conversion type");
        }
        return std::make_unique<IdentifierExpression>(conversion);
    }
    if (match({TokenType::LeftParen})) {
        const SourceLocation groupingLocation = previous().location;
        ExpressionPtr expression = parseExpression();
        consume(TokenType::RightParen, "expected ')' after the expression");
        return std::make_unique<GroupingExpression>(groupingLocation,
                                                    std::move(expression));
    }
    fail(peek(), "expected an expression");
}

bool Parser::isVariableDeclarationStart() const {
    if (check(TokenType::Var)) {
        return true;
    }
    std::size_t afterType = 1;
    bool pointer = false;
    while (true) {
        if (peek(afterType).type == TokenType::Star) {
            pointer = true;
            ++afterType;
        } else if (peek(afterType).type == TokenType::LeftBracket &&
                   peek(afterType + 1).type == TokenType::RightBracket) afterType += 2;
        else break;
    }
    // Without a symbol table, `a * b;` is multiplication. A named pointer
    // declaration is distinguishable by its required initializer.
    if (pointer && peek().type == TokenType::Identifier &&
        peek(afterType + 1).type != TokenType::Assign) return false;
    if (isTypeStart(peek().type) && peek(afterType).type == TokenType::Identifier) {
        return true;
    }
    return false;
}

bool Parser::isTypeStart(TokenType type) const {
    return type == TokenType::IntType || type == TokenType::FloatType ||
           type == TokenType::BoolType || type == TokenType::StringType ||
           type == TokenType::Identifier;
}

bool Parser::isAssignable(const Expression& expression) const {
    if (expression.kind == ExpressionKind::Grouping) {
        return isAssignable(*static_cast<const GroupingExpression&>(expression).expression);
    }
    if (expression.kind == ExpressionKind::Unary) {
        return static_cast<const UnaryExpression&>(expression).operation.type == TokenType::Star;
    }
    return expression.kind == ExpressionKind::Identifier ||
           expression.kind == ExpressionKind::Member ||
           expression.kind == ExpressionKind::Index;
}

bool Parser::isStatementStart(TokenType type) const {
    return type == TokenType::Var || type == TokenType::Return ||
           type == TokenType::If || type == TokenType::While ||
           type == TokenType::For || type == TokenType::Unsafe || isTypeStart(type);
}

bool Parser::isDeclarationStart() const {
    return check(TokenType::Fn) || check(TokenType::Extern) || check(TokenType::Struct) ||
           check(TokenType::Import) || isVariableDeclarationStart();
}

void Parser::consumeStatementTerminator() {
    consume(TokenType::Semicolon, "expected ';' after the statement");
    skipNewlines();
}

void Parser::skipNewlines() {
    while (match({TokenType::Newline})) {
    }
}

void Parser::synchronize(bool insideBlock) {
    const std::size_t recoveryStart = current_;
    while (!atEnd()) {
        if (check(TokenType::Dedent)) {
            if (insideBlock) {
                return;
            }
            advance();
            continue;
        }

        if (current_ > recoveryStart &&
            (previous().type == TokenType::Semicolon ||
             previous().type == TokenType::Newline)) {
            return;
        }
        if (insideBlock && isStatementStart(peek().type)) {
            return;
        }
        if (!insideBlock && isDeclarationStart()) {
            return;
        }
        advance();
    }
}

void Parser::report(const Token& token, std::string message) {
    result_.diagnostics.push_back({token.location, std::move(message)});
}

[[noreturn]] void Parser::fail(const Token& token, std::string message) {
    report(token, std::move(message));
    throw ParseError{};
}

bool Parser::match(std::initializer_list<TokenType> types) {
    for (const TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

const Token& Parser::consume(TokenType type, std::string message) {
    if (check(type)) {
        return advance();
    }
    fail(peek(), std::move(message));
}

bool Parser::check(TokenType type) const {
    return peek().type == type;
}

const Token& Parser::peek(std::size_t offset) const {
    const std::size_t index = current_ + offset;
    return index < tokens_.size() ? tokens_[index] : tokens_.back();
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

const Token& Parser::advance() {
    if (!atEnd()) {
        ++current_;
    }
    return previous();
}

bool Parser::atEnd() const {
    return check(TokenType::EndOfFile);
}

} // namespace basicc
