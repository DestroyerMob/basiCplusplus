#include "compiler.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace basicc {
namespace {

enum class Type { Int, Float, Bool, String, Any, Void, AggregateStart };

bool isNumeric(Type type) {
    return type == Type::Int || type == Type::Float;
}

std::optional<Type> conversionType(const std::string& name) {
    if (name == "int" || name == "integer") return Type::Int;
    if (name == "float" || name == "double") return Type::Float;
    if (name == "string" || name == "str") return Type::String;
    return std::nullopt;
}

bool isBuiltinFunction(const std::string& name) {
    return name == "print" || name == "input" || name == "len" ||
           name == "any" || name == "cast" || name == "type_name";
}

std::optional<double> floatingValue(const std::string& spelling) {
    // A source literal always uses '.', independently of the machine's locale.
    std::istringstream input(spelling);
    input.imbue(std::locale::classic());
    double value;
    if (!(input >> value) || !std::isfinite(value)) return std::nullopt;
    return value;
}

struct SemanticError {};
enum class Visit { Unchecked, Checking, Checked };

struct FunctionInfo {
    const FunctionDeclaration* declaration;
    std::optional<Type> result;
    Visit visit = Visit::Unchecked;
};

struct AggregateInfo {
    std::string name;
    const StructDeclaration* structure = nullptr;
    std::optional<Type> element;
    std::vector<Type> fields;
    Visit visit = Visit::Unchecked;
    std::optional<Type> pointee{};
};

// Pointer lifetimes form a small flow graph. A variable depends on every value
// ever assigned to it, including assignments later in a loop. Solving after
// checking the whole program prevents an earlier return from missing a borrow
// that could reach it on a later iteration. This deliberately favors rejecting
// an uncertain escape over attempting path-sensitive lifetime inference.
class Lifetimes {
public:
    std::size_t value(unsigned depth = 0) {
        nodes_.push_back({depth, {}});
        return nodes_.size() - 1;
    }
    void assign(std::size_t target, std::size_t source) { nodes_[target].sources.push_back(source); }
    void limit(std::size_t node, unsigned depth, SourceLocation location, std::string message) {
        limits_.push_back({node, depth, std::move(location), std::move(message)});
    }
    std::optional<Diagnostic> check() {
        bool changed;
        do {
            changed = false;
            for (auto& node : nodes_) {
                for (const auto source : node.sources) {
                    if (nodes_[source].depth > node.depth) {
                        node.depth = nodes_[source].depth;
                        changed = true;
                    }
                }
            }
        } while (changed);
        for (const auto& limit : limits_) {
            if (nodes_[limit.node].depth > limit.depth) return Diagnostic{limit.location, limit.message};
        }
        return std::nullopt;
    }
private:
    struct Node { unsigned depth; std::vector<std::size_t> sources; };
    struct Limit { std::size_t node; unsigned depth; SourceLocation location; std::string message; };
    // Depth 0 is process-lifetime storage, 1 is a borrowed parameter, and
    // depths >= 2 are local scopes. Node 0 is the shared stable/null origin.
    std::vector<Node> nodes_{{0, {}}};
    std::vector<Limit> limits_;
};

struct Symbol {
    Type type;
    unsigned storageDepth;
    std::size_t pointerLife = 0;
};

// Semantic information lives beside the AST, rather than modifying parser
// nodes. Generation only runs after every declaration has passed checking.
class Compiler {
public:
    explicit Compiler(const Program& program) : program_(program) {}
    CompileResult run();

private:
    using Scope = std::unordered_map<std::string, Symbol>;
    bool isPointer(Type type) const { return type >= Type::AggregateStart && aggregate(type).pointee.has_value(); }
    bool isAggregate(Type type) const { return type >= Type::AggregateStart && !isPointer(type); }
    bool isArray(Type type) const { return isAggregate(type) && aggregate(type).element.has_value(); }
    AggregateInfo& aggregate(Type type);
    const AggregateInfo& aggregate(Type type) const;
    std::string typeName(Type type) const;
    std::string cType(Type type) const;
    Type arrayType(Type element);
    Type pointerType(Type pointee, SourceLocation location);
    void resolveStruct(Type type);
    Type fieldType(Type type, const std::string& name, SourceLocation location);
    bool isMutableTarget(const Expression& expression) const;
    [[noreturn]] void fail(SourceLocation location, std::string message);
    Type resolveType(const TypeSyntax& syntax, bool allowVoid = false);
    Type foreignType(const TypeSyntax& syntax, bool allowVoid = false);
    std::string foreignCType(const TypeSyntax& syntax);
    void require(Type actual, Type expected, SourceLocation location);
    void declare(const std::string& name, Type type, SourceLocation location, std::size_t origin = 0);
    Symbol& symbol(const std::string& name, SourceLocation location);
    std::size_t addressLife(const Expression& expression);
    void requireUnsafe(SourceLocation location);
    Type checkFunction(FunctionInfo& function);
    Type checkExpression(const Expression& expression, std::optional<Type> expected = std::nullopt);
    Type checkValue(const Expression& expression, std::optional<Type> expected = std::nullopt);
    void checkBinding(const VariableBinding& binding);
    bool checkStatement(const Statement& statement);
    bool checkBlock(const BlockStatement& block, bool newScope = true);
    void checkCondition(const Expression& expression);
    void checkComparison(Type left, Type right, const Token& operation);

    void line(const std::string& text);
    std::string save(Type type, const std::string& value);
    std::string emitExpression(const Expression& expression);
    std::string emitAddress(const Expression& expression);
    std::string comparisonCode(Type type, const std::string& left,
                               const Token& operation, const std::string& right) const;
    std::string stringCode(Type type, const std::string& value) const;
    void emitAggregateTypes();
    void emitAnyRuntime();
    void emitStatement(const Statement& statement);
    void emitBinding(const VariableBinding& binding, bool global = false);
    void emitBlock(const BlockStatement& block);
    std::string signature(const FunctionDeclaration& function);
    std::string generate();
    std::string generateCpp();

    const Program& program_;
    CompileResult result_;
    Scope globals_;
    std::vector<Scope> scopes_;
    std::unordered_map<std::string, FunctionInfo> functions_;
    // Scalar IDs are fixed; structs are nominal types and arrays are interned
    // by element type. IDs stay stable while recursive type resolution grows
    // the registry, unlike references into the vector.
    std::vector<AggregateInfo> aggregateTypes_;
    std::vector<Type> aggregateOrder_;
    std::unordered_map<std::string, Type> namedTypes_;
    std::unordered_map<Type, Type> arrayTypes_;
    std::unordered_map<Type, Type> pointerTypes_;
    Lifetimes lifetimes_;
    std::unordered_map<const Expression*, std::size_t> pointerLives_;
    unsigned unsafeDepth_ = 0;
    std::unordered_map<const Expression*, Type> types_;
    std::unordered_map<const VariableBinding*, Type> bindingTypes_;
    std::optional<Type> returnType_;
    bool globalInitializer_ = false;
    std::string output_;
    unsigned depth_ = 0;
    unsigned temporary_ = 0;
};

AggregateInfo& Compiler::aggregate(Type type) {
    return aggregateTypes_.at(static_cast<std::size_t>(type) -
                              static_cast<std::size_t>(Type::AggregateStart));
}

const AggregateInfo& Compiler::aggregate(Type type) const {
    return aggregateTypes_.at(static_cast<std::size_t>(type) -
                              static_cast<std::size_t>(Type::AggregateStart));
}

std::string Compiler::typeName(Type type) const {
    switch (type) {
    case Type::Int: return "int";
    case Type::Float: return "float";
    case Type::Bool: return "bool";
    case Type::String: return "string";
    case Type::Any: return "any";
    case Type::Void: return "void";
    default: return aggregate(type).name;
    }
}

std::string Compiler::cType(Type type) const {
    if (isPointer(type)) return cType(*aggregate(type).pointee) + "*";
    if (isAggregate(type)) return "bc_type" + std::to_string(static_cast<unsigned>(type));
    if (type == Type::Int) return "int64_t";
    if (type == Type::Float) return "double";
    if (type == Type::String) return "bc_string";
    if (type == Type::Any) return "bc_any";
    return typeName(type);
}

Type Compiler::arrayType(Type element) {
    const auto existing = arrayTypes_.find(element);
    if (existing != arrayTypes_.end()) return existing->second;
    const Type type = static_cast<Type>(static_cast<std::size_t>(Type::AggregateStart) + aggregateTypes_.size());
    aggregateTypes_.push_back({typeName(element) + "[]", nullptr, element, {}, Visit::Checked});
    arrayTypes_.emplace(element, type);
    aggregateOrder_.push_back(type);
    return type;
}

Type Compiler::pointerType(Type pointee, SourceLocation location) {
    if (isPointer(pointee)) fail(location, "pointers to pointer variables are not supported yet");
    const auto existing = pointerTypes_.find(pointee);
    if (existing != pointerTypes_.end()) return existing->second;
    const Type type = static_cast<Type>(static_cast<std::size_t>(Type::AggregateStart) + aggregateTypes_.size());
    aggregateTypes_.push_back({typeName(pointee) + "*", nullptr, std::nullopt, {}, Visit::Checked, pointee});
    pointerTypes_.emplace(pointee, type);
    return type;
}

void Compiler::resolveStruct(Type type) {
    if (aggregate(type).visit == Visit::Checked) return;
    const auto* structure = aggregate(type).structure;
    if (aggregate(type).visit == Visit::Checking) {
        fail(structure->location, "recursive struct types are not supported yet");
    }
    aggregate(type).visit = Visit::Checking;
    std::unordered_map<std::string, Type> fields;
    for (const auto& field : structure->fields) {
        const Type fieldType = resolveType(field.type);
        if (isPointer(fieldType)) fail(field.location, "pointer fields are not supported by the initial lifetime checker");
        if (!fields.emplace(field.name, fieldType).second) {
            fail(field.location, "duplicate field '" + field.name + "'");
        }
        aggregate(type).fields.push_back(fieldType);
    }
    aggregate(type).visit = Visit::Checked;
    aggregateOrder_.push_back(type);
}

[[noreturn]] void Compiler::fail(SourceLocation location, std::string message) {
    result_.diagnostics.push_back({location, std::move(message)});
    throw SemanticError{};
}

Type Compiler::resolveType(const TypeSyntax& syntax, bool allowVoid) {
    Type type = Type::Void;
    switch (syntax.kind) {
    case TypeKind::Int: type = Type::Int; break;
    case TypeKind::Float: type = Type::Float; break;
    case TypeKind::Bool: type = Type::Bool; break;
    case TypeKind::String: type = Type::String; break;
    case TypeKind::Named: {
        if (syntax.name == "any") { type = Type::Any; break; }
        if (allowVoid && syntax.name == "void" && syntax.suffixes.empty()) return Type::Void;
        const auto named = namedTypes_.find(syntax.name);
        if (named == namedTypes_.end()) fail(syntax.location, "unknown type '" + syntax.name + "'");
        type = named->second;
        resolveStruct(type);
        break;
    }
    }
    for (const auto suffix : syntax.suffixes) {
        if (suffix == TypeSuffix::Pointer) type = pointerType(type, syntax.location);
        else {
            if (isPointer(type)) fail(syntax.location, "arrays of pointers are not supported by the initial lifetime checker");
            type = arrayType(type);
        }
    }
    return type;
}

Type Compiler::fieldType(Type type, const std::string& name, SourceLocation location) {
    if ((type == Type::String || isArray(type)) && name == "length") return Type::Int;
    if (isAggregate(type) && aggregate(type).structure) {
        const auto& info = aggregate(type);
        for (std::size_t i = 0; i < info.fields.size(); ++i) {
            if (info.structure->fields[i].name == name) return info.fields[i];
        }
    }
    fail(location, "type '" + typeName(type) + "' has no member '" + name + "'");
}

Type Compiler::foreignType(const TypeSyntax& syntax, bool allowVoid) {
    // c_int describes the platform C ABI, but callers still use BasicC's int.
    // Narrowing is checked at the call boundary rather than becoming an
    // implicit conversion throughout the language.
    if (syntax.name == "c_int" && syntax.suffixes.empty()) return Type::Int;
    const Type type = resolveType(syntax, allowVoid);
    if (isPointer(type)) {
        const Type pointee = *aggregate(type).pointee;
        if (pointee == Type::Int || pointee == Type::Float || pointee == Type::Bool) return type;
        fail(syntax.location, "extern pointers currently require int, float, or bool pointees");
    }
    if (type == Type::String || type == Type::Any || isAggregate(type)) {
        fail(syntax.location, "extern signatures currently support int, c_int, float, bool, and void results");
    }
    return type;
}

std::string Compiler::foreignCType(const TypeSyntax& syntax) {
    if (syntax.name == "c_int" && syntax.suffixes.empty()) return "int";
    return cType(foreignType(syntax, true));
}

bool Compiler::isMutableTarget(const Expression& expression) const {
    switch (expression.kind) {
    case ExpressionKind::Unary:
        return static_cast<const UnaryExpression&>(expression).operation.type == TokenType::Star;
    case ExpressionKind::Identifier: return true;
    case ExpressionKind::Grouping:
        return isMutableTarget(*static_cast<const GroupingExpression&>(expression).expression);
    case ExpressionKind::Member: {
        const auto& object = *static_cast<const MemberExpression&>(expression).object;
        const Type type = types_.at(&object);
        return isAggregate(type) && !isArray(type) && isMutableTarget(object);
    }
    case ExpressionKind::Index:
        return isArray(types_.at(static_cast<const IndexExpression&>(expression).object.get()));
    default: return false;
    }
}

void Compiler::require(Type actual, Type expected, SourceLocation location) {
    if (actual != expected) {
        fail(location, "expected " + std::string(typeName(expected)) +
                       ", got " + typeName(actual));
    }
}

void Compiler::declare(const std::string& name, Type type, SourceLocation location, std::size_t origin) {
    Scope& scope = scopes_.empty() ? globals_ : scopes_.back();
    const unsigned storage = scopes_.empty() ? 0 : static_cast<unsigned>(scopes_.size()) + 1;
    std::size_t life = 0;
    if (isPointer(type)) {
        life = lifetimes_.value();
        lifetimes_.assign(life, origin);
        lifetimes_.limit(origin, storage, location, "pointer may outlive the storage it borrows");
    }
    if (isBuiltinFunction(name) || (scopes_.empty() && (functions_.count(name) || namedTypes_.count(name))) ||
        !scope.emplace(name, Symbol{type, storage, life}).second) {
        fail(location, "duplicate or reserved declaration '" + name + "'");
    }
}

Symbol& Compiler::symbol(const std::string& name, SourceLocation location) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->find(name);
        if (found != scope->end()) return found->second;
    }
    const auto found = globals_.find(name);
    if (found != globals_.end()) return found->second;
    fail(location, "undefined variable '" + name + "'");
}

void Compiler::requireUnsafe(SourceLocation location) {
    if (!unsafeDepth_) fail(location, "this operation requires an unsafe block");
}

std::size_t Compiler::addressLife(const Expression& expression) {
    switch (expression.kind) {
    case ExpressionKind::Identifier:
        return lifetimes_.value(symbol(static_cast<const IdentifierExpression&>(expression).name,
                                      expression.location).storageDepth);
    case ExpressionKind::Grouping:
        return addressLife(*static_cast<const GroupingExpression&>(expression).expression);
    case ExpressionKind::Member:
        return addressLife(*static_cast<const MemberExpression&>(expression).object);
    case ExpressionKind::Unary:
        return pointerLives_.at(static_cast<const UnaryExpression&>(expression).operand.get());
    case ExpressionKind::Index:
        return 0; // Array elements live in the process-lifetime arena.
    default: return 0; // Address-of already rejected non-addressable expressions.
    }
}

void Compiler::checkBinding(const VariableBinding& binding) {
    // Resolve the initializer before entering the name into its scope. This
    // lets an inner declaration deliberately initialize from an outer value.
    const auto annotation = binding.explicitType
        ? std::optional<Type>(resolveType(*binding.explicitType)) : std::nullopt;
    const Type initializer = checkValue(*binding.initializer, annotation);
    const Type type = annotation.value_or(initializer);
    require(initializer, type, binding.location);
    declare(binding.name, type, binding.location,
            isPointer(type) ? pointerLives_.at(binding.initializer.get()) : 0);
    bindingTypes_[&binding] = type;
}

Type Compiler::checkFunction(FunctionInfo& info) {
    if (info.visit == Visit::Checked) return *info.result;
    const auto& function = *info.declaration;
    if (info.visit == Visit::Checking) {
        if (info.result) return *info.result;
        fail(function.location, "recursive function '" + function.name +
             "' needs an explicit return type");
    }
    info.visit = Visit::Checking;

    // Calls may require checking a later function to infer its result. Save
    // the caller's locals: a callee can see globals, never its caller's scope.
    auto callerScopes = std::move(scopes_);
    const auto callerReturn = returnType_;
    const auto callerUnsafe = unsafeDepth_;
    unsafeDepth_ = 0;
    scopes_.clear();
    scopes_.emplace_back();
    returnType_ = info.result;
    for (const auto& parameter : function.parameters) {
        const Type type = function.external ? foreignType(parameter.type) : resolveType(parameter.type);
        declare(parameter.name, type, parameter.location, isPointer(type) ? lifetimes_.value(1) : 0);
    }
    const bool alwaysReturns = function.external || checkBlock(*function.body, false);
    info.result = returnType_.value_or(Type::Void);
    if (*info.result != Type::Void && !alwaysReturns) {
        fail(function.location, "function '" + function.name +
             "' must return a value on every path");
    }
    info.visit = Visit::Checked;
    scopes_ = std::move(callerScopes);
    returnType_ = callerReturn;
    unsafeDepth_ = callerUnsafe;
    return *info.result;
}

Type Compiler::checkValue(const Expression& expression, std::optional<Type> expected) {
    const Type type = checkExpression(expression, expected);
    if (type == Type::Void) fail(expression.location, "a void call has no value");
    return type;
}

void Compiler::checkComparison(Type left, Type right, const Token& operation) {
    require(right, left, operation.location);
    if (operation.type != TokenType::EqualEqual &&
        operation.type != TokenType::BangEqual) {
        if (!isNumeric(left) && left != Type::Any) fail(operation.location, "ordering comparisons require numeric values");
    }
}

Type Compiler::checkExpression(const Expression& expression, std::optional<Type> expected) {
    Type type = Type::Void;
    switch (expression.kind) {
    case ExpressionKind::Cast: {
        const auto& cast = static_cast<const CastExpression&>(expression);
        require(checkValue(*cast.value), Type::Any, expression.location);
        type = resolveType(cast.target);
        if (isPointer(type)) fail(expression.location, "any cannot contain or produce a borrowed pointer");
        break;
    }
    case ExpressionKind::Array: {
        const auto& array = static_cast<const ArrayExpression&>(expression);
        std::optional<Type> element;
        if (expected && isArray(*expected)) element = aggregate(*expected).element;
        for (const auto& value : array.elements) {
            const Type actual = checkValue(*value, element);
            if (!element) element = actual;
            require(actual, *element, value->location);
        }
        if (!element) fail(expression.location, "an empty array needs an explicit element type");
        if (isPointer(*element)) fail(expression.location, "arrays of pointers are not supported by the initial lifetime checker");
        type = arrayType(*element);
        break;
    }
    case ExpressionKind::Literal: {
        const auto& token = static_cast<const LiteralExpression&>(expression).token;
        if (token.type == TokenType::Null) {
            if (!expected || !isPointer(*expected)) fail(token.location, "null needs a pointer type context");
            type = *expected;
            pointerLives_[&expression] = 0;
        } else if (token.type == TokenType::True || token.type == TokenType::False) {
            type = Type::Bool;
        } else if (token.type == TokenType::IntegerLiteral) {
            std::int64_t value;
            const auto parsed = std::from_chars(token.lexeme.data(),
                token.lexeme.data() + token.lexeme.size(), value);
            if (parsed.ec != std::errc{}) {
                fail(token.location, "integer literal is outside the signed 64-bit range");
            }
            type = Type::Int;
        } else if (token.type == TokenType::FloatLiteral) {
            if (!floatingValue(token.lexeme)) {
                fail(token.location, "float literal is outside the finite 64-bit range");
            }
            type = Type::Float;
        } else {
            type = Type::String;
        }
        break;
    }
    case ExpressionKind::Identifier: {
        const auto& value = symbol(static_cast<const IdentifierExpression&>(expression).name, expression.location);
        type = value.type;
        if (isPointer(type)) pointerLives_[&expression] = value.pointerLife;
        break;
    }
    case ExpressionKind::Grouping: {
        const auto& inner = *static_cast<const GroupingExpression&>(expression).expression;
        type = checkExpression(inner, expected);
        if (isPointer(type)) pointerLives_[&expression] = pointerLives_.at(&inner);
        break;
    }
    case ExpressionKind::Unary: {
        const auto& unary = static_cast<const UnaryExpression&>(expression);
        // The magnitude of INT64_MIN does not fit in a positive int64_t.
        // Recognize this spelling before checking the positive literal alone.
        if (unary.operation.type == TokenType::Minus &&
            unary.operand->kind == ExpressionKind::Literal &&
            static_cast<const LiteralExpression&>(*unary.operand).token.lexeme ==
                "9223372036854775808") {
            types_[unary.operand.get()] = Type::Int;
            type = Type::Int;
            break;
        }
        if (unary.operation.type == TokenType::Ampersand) {
            requireUnsafe(expression.location);
            const Type pointee = checkValue(*unary.operand);
            if (!isMutableTarget(*unary.operand)) fail(expression.location, "address-of requires mutable storage");
            type = pointerType(pointee, expression.location);
            pointerLives_[&expression] = addressLife(*unary.operand);
            break;
        }
        if (unary.operation.type == TokenType::Star) {
            requireUnsafe(expression.location);
            const Type pointer = checkValue(*unary.operand);
            if (!isPointer(pointer)) fail(expression.location, "dereferencing requires a pointer");
            type = *aggregate(pointer).pointee;
            break;
        }
        type = checkValue(*unary.operand);
        if (unary.operation.type == TokenType::Bang) require(type, Type::Bool, expression.location);
        else if (!isNumeric(type) && type != Type::Any) fail(expression.location, "unary '+' and '-' require a numeric value");
        break;
    }
    case ExpressionKind::Binary: {
        const auto& binary = static_cast<const BinaryExpression&>(expression);
        const Type left = checkValue(*binary.left);
        const Type right = checkValue(*binary.right, left);
        switch (binary.operation.type) {
        case TokenType::AmpersandAmpersand: case TokenType::PipePipe:
            require(left, Type::Bool, expression.location);
            require(right, Type::Bool, expression.location);
            type = Type::Bool;
            break;
        case TokenType::EqualEqual: case TokenType::BangEqual:
        case TokenType::Less: case TokenType::LessEqual:
        case TokenType::Greater: case TokenType::GreaterEqual:
            checkComparison(left, right, binary.operation);
            type = Type::Bool;
            break;
        case TokenType::Plus: case TokenType::Minus:
        case TokenType::Star: case TokenType::Slash:
            if (!isNumeric(left) && left != Type::Any &&
                !(left == Type::String && binary.operation.type == TokenType::Plus)) {
                fail(expression.location, "arithmetic requires numeric values; '+' also concatenates strings");
            }
            require(right, left, expression.location);
            type = left;
            break;
        default:
            if (left != Type::Any) require(left, Type::Int, expression.location);
            require(right, left, expression.location);
            type = left;
            break;
        }
        break;
    }
    case ExpressionKind::SharedComparison: {
        const auto& shared = static_cast<const SharedComparisonExpression&>(expression);
        const Type operandType = checkValue(*shared.operands.front());
        for (std::size_t i = 1; i < shared.operands.size(); ++i) {
            require(checkValue(*shared.operands[i], operandType), operandType,
                    shared.operands[i]->location);
        }
        checkComparison(operandType, checkValue(*shared.state, operandType), shared.operation);
        type = Type::Bool;
        break;
    }
    case ExpressionKind::Conditional: {
        const auto& conditional = static_cast<const ConditionalExpression&>(expression);
        checkCondition(*conditional.condition);
        type = checkValue(*conditional.whenTrue, expected);
        require(checkValue(*conditional.whenFalse, type), type, expression.location);
        if (isPointer(type)) {
            const auto life = lifetimes_.value();
            lifetimes_.assign(life, pointerLives_.at(conditional.whenTrue.get()));
            lifetimes_.assign(life, pointerLives_.at(conditional.whenFalse.get()));
            pointerLives_[&expression] = life;
        }
        break;
    }
    case ExpressionKind::Assignment: {
        const auto& assignment = static_cast<const AssignmentExpression&>(expression);
        if (globalInitializer_) {
            fail(expression.location, "global initializers cannot assign to other variables");
        }
        type = checkValue(*assignment.target);
        if (!isMutableTarget(*assignment.target)) fail(expression.location, "assignment target is read-only");
        require(checkValue(*assignment.value, type), type, expression.location);
        if (isPointer(type)) {
            const Expression* target = assignment.target.get();
            while (target->kind == ExpressionKind::Grouping) {
                target = static_cast<const GroupingExpression*>(target)->expression.get();
            }
            // Pointer fields, pointer elements, and pointer-to-pointer types
            // are excluded, so a pointer-valued store has one tracked variable.
            auto& destination = symbol(static_cast<const IdentifierExpression*>(target)->name, target->location);
            const auto origin = pointerLives_.at(assignment.value.get());
            lifetimes_.assign(destination.pointerLife, origin);
            lifetimes_.limit(origin, destination.storageDepth, expression.location,
                             "cannot store a pointer into a longer-lived variable");
            pointerLives_[&expression] = origin;
        }
        break;
    }
    case ExpressionKind::Call: {
        const auto& call = static_cast<const CallExpression&>(expression);
        const Expression* callee = call.callee.get();
        while (callee->kind == ExpressionKind::Grouping) {
            callee = static_cast<const GroupingExpression*>(callee)->expression.get();
        }
        if (callee->kind != ExpressionKind::Identifier) {
            fail(expression.location, "only named function calls can execute yet");
        }
        const auto& name = static_cast<const IdentifierExpression*>(callee)->name;
        if (name == "any") {
            if (call.arguments.size() != 1) fail(expression.location, "any expects one value");
            if (isPointer(checkValue(*call.arguments.front()))) {
                fail(expression.location, "any cannot hide a borrowed pointer");
            }
            type = Type::Any;
            break;
        }
        if (const auto target = conversionType(name)) {
            if (call.arguments.size() != 1) {
                fail(expression.location, "conversion expects one argument");
            }
            const Type source = checkValue(*call.arguments.front());
            if (*target != Type::String && !isNumeric(source) && source != Type::String && source != Type::Any) {
                fail(expression.location, "numeric conversion requires int, float, or string");
            }
            type = *target;
            break;
        }
        for (const auto& scope : scopes_) {
            if (scope.count(name)) fail(expression.location, "variable '" + name + "' is not callable");
        }
        if (globals_.count(name)) fail(expression.location, "variable '" + name + "' is not callable");
        if (const auto named = namedTypes_.find(name); named != namedTypes_.end()) {
            type = named->second;
            // Copy the field list: nested array literals can grow the type registry.
            const auto fields = aggregate(type).fields;
            if (call.arguments.size() != fields.size()) {
                fail(expression.location, "constructor '" + name + "' requires one argument per field");
            }
            for (std::size_t i = 0; i < fields.size(); ++i) {
                require(checkValue(*call.arguments[i], fields[i]), fields[i], call.arguments[i]->location);
            }
            break;
        }
        if (globalInitializer_) {
            fail(expression.location, "global initializers cannot call functions yet");
        }
        if (name == "print") {
            if (call.arguments.size() != 1) fail(expression.location, "print expects one argument");
            checkValue(*call.arguments.front());
            type = Type::Void;
            break;
        }
        if (name == "input") {
            if (!call.arguments.empty()) fail(expression.location, "input expects no arguments");
            type = Type::String;
            break;
        }
        if (name == "len") {
            if (call.arguments.size() != 1) fail(expression.location, "len expects one argument");
            const Type argument = checkValue(*call.arguments.front());
            if (argument != Type::String && argument != Type::Any && !isArray(argument)) {
                fail(expression.location, "len requires a string or array");
            }
            type = Type::Int;
            break;
        }
        if (name == "type_name") {
            if (call.arguments.size() != 1) fail(expression.location, "type_name expects one value");
            checkValue(*call.arguments.front());
            type = Type::String;
            break;
        }
        const auto found = functions_.find(name);
        if (found == functions_.end()) fail(expression.location, "undefined function '" + name + "'");
        if (found->second.declaration->external) requireUnsafe(expression.location);
        const auto& parameters = found->second.declaration->parameters;
        if (call.arguments.size() != parameters.size()) {
            fail(expression.location, "wrong number of arguments to '" + name + "'");
        }
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            const Type parameterType = found->second.declaration->external
                ? foreignType(parameters[i].type) : resolveType(parameters[i].type);
            require(checkValue(*call.arguments[i], parameterType), parameterType,
                    call.arguments[i]->location);
        }
        type = checkFunction(found->second);
        if (isPointer(type)) {
            const auto life = lifetimes_.value();
            // A pointer result may borrow from any pointer argument. Callee
            // returns are separately checked against escaping their own locals.
            for (const auto& argument : call.arguments) {
                if (isPointer(types_.at(argument.get()))) lifetimes_.assign(life, pointerLives_.at(argument.get()));
            }
            pointerLives_[&expression] = life;
        }
        break;
    }
    case ExpressionKind::Index: {
        const auto& index = static_cast<const IndexExpression&>(expression);
        const Type object = checkValue(*index.object);
        require(checkValue(*index.index), Type::Int, index.index->location);
        if (object == Type::String) type = Type::String;
        else if (isArray(object)) type = *aggregate(object).element;
        else fail(expression.location, "indexing requires an array or string");
        break;
    }
    case ExpressionKind::Member: {
        const auto& member = static_cast<const MemberExpression&>(expression);
        type = fieldType(checkValue(*member.object), member.member, expression.location);
        break;
    }
    }
    types_[&expression] = type;
    return type;
}

void Compiler::checkCondition(const Expression& expression) {
    require(checkValue(expression), Type::Bool, expression.location);
}

bool Compiler::checkBlock(const BlockStatement& block, bool newScope) {
    if (newScope) scopes_.emplace_back();
    bool returns = false;
    for (const auto& statement : block.statements) {
        // Still check unreachable code, so invalid declarations never reach C.
        const bool childReturns = checkStatement(*statement);
        returns = returns || childReturns;
    }
    if (newScope) scopes_.pop_back();
    return returns;
}

bool Compiler::checkStatement(const Statement& statement) {
    switch (statement.kind) {
    case StatementKind::Unsafe: {
        ++unsafeDepth_;
        const bool returns = checkBlock(*static_cast<const UnsafeStatement&>(statement).body);
        --unsafeDepth_;
        return returns;
    }
    case StatementKind::Block:
        return checkBlock(static_cast<const BlockStatement&>(statement));
    case StatementKind::VariableDeclaration:
        checkBinding(static_cast<const VariableDeclarationStatement&>(statement).binding);
        break;
    case StatementKind::Expression:
        checkExpression(*static_cast<const ExpressionStatement&>(statement).expression);
        break;
    case StatementKind::Return: {
        const auto& returned = static_cast<const ReturnStatement&>(statement);
        const Type type = returned.value ? checkValue(*returned.value, returnType_) : Type::Void;
        if (isPointer(type)) {
            lifetimes_.limit(pointerLives_.at(returned.value.get()), 1, statement.location,
                             "cannot return a pointer that may refer to a local variable");
        }
        if (returnType_) require(type, *returnType_, statement.location);
        else returnType_ = type;
        return true;
    }
    case StatementKind::If: {
        const auto& branch = static_cast<const IfStatement&>(statement);
        checkCondition(*branch.condition);
        const bool thenReturns = checkBlock(*branch.thenBranch);
        const bool elseReturns = branch.elseBranch && checkStatement(*branch.elseBranch);
        return thenReturns && elseReturns;
    }
    case StatementKind::While: {
        const auto& loop = static_cast<const WhileStatement&>(statement);
        checkCondition(*loop.condition);
        checkBlock(*loop.body);
        // There is no break statement yet, so a literal true loop cannot fall through.
        return loop.condition->kind == ExpressionKind::Literal &&
            static_cast<const LiteralExpression&>(*loop.condition).token.type == TokenType::True;
    }
    case StatementKind::For: {
        const auto& loop = static_cast<const ForStatement&>(statement);
        scopes_.emplace_back();
        if (loop.initializer) checkStatement(*loop.initializer);
        if (loop.condition) checkCondition(*loop.condition);
        if (loop.increment) checkExpression(*loop.increment);
        checkBlock(*loop.body);
        scopes_.pop_back();
        return !loop.condition;
    }
    }
    return false;
}

void Compiler::line(const std::string& text) {
    output_.append(depth_ * 4, ' ');
    output_ += text + '\n';
}

std::string Compiler::save(Type type, const std::string& value) {
    const std::string name = "bc_t" + std::to_string(temporary_++);
    line(std::string(cType(type)) + " " + name + " = " + value + ";");
    return name;
}

std::string Compiler::comparisonCode(Type type, const std::string& left,
                                     const Token& operation, const std::string& right) const {
    if (type == Type::Any) {
        if (operation.type == TokenType::EqualEqual || operation.type == TokenType::BangEqual) {
            return std::string(operation.type == TokenType::BangEqual ? "!" : "") +
                "bc_any_equal(" + left + ", " + right + ")";
        }
        return "bc_any_compare(" + std::to_string(static_cast<int>(operation.type)) + ", " + left + ", " + right + ")";
    }
    if (type == Type::String || isAggregate(type)) {
        const std::string helper = type == Type::String ? "bc_string_equal" : cType(type) + "_equal";
        return std::string(operation.type == TokenType::BangEqual ? "!" : "") +
               helper + "(" + left + ", " + right + ")";
    }
    return "(" + left + " " + operation.lexeme + " " + right + ")";
}

std::string Compiler::stringCode(Type type, const std::string& value) const {
    if (type == Type::Any) return "bc_any_string(" + value + ")";
    if (isPointer(type)) return "bc_string_from_pointer((const void*)(" + value + "))";
    if (type == Type::String) return value;
    const std::string helper = isAggregate(type) ? cType(type) + "_string"
                                                : "bc_string_from_" + typeName(type);
    return helper + "(" + value + ")";
}

std::string Compiler::emitAddress(const Expression& expression) {
    switch (expression.kind) {
    case ExpressionKind::Unary: {
        const auto& unary = static_cast<const UnaryExpression&>(expression);
        const std::string pointer = emitExpression(*unary.operand);
        line("if (" + pointer + " == NULL) bc_fail(\"null pointer dereference\");");
        return pointer;
    }
    case ExpressionKind::Identifier:
        return "&bc_v_" + static_cast<const IdentifierExpression&>(expression).name;
    case ExpressionKind::Grouping:
        return emitAddress(*static_cast<const GroupingExpression&>(expression).expression);
    case ExpressionKind::Member: {
        const auto& member = static_cast<const MemberExpression&>(expression);
        const std::string object = emitAddress(*member.object);
        return "&((" + object + ")->bc_m_" + member.member + ")";
    }
    case ExpressionKind::Index: {
        const auto& index = static_cast<const IndexExpression&>(expression);
        const Type array = types_.at(index.object.get());
        const std::string object = emitExpression(*index.object);
        const std::string subscript = emitExpression(*index.index);
        const std::string pointer = "bc_p" + std::to_string(temporary_++);
        // Resolve and bounds-check the destination before the RHS runs. Saving
        // this pointer prevents RHS side effects from changing which slot is written.
        line(cType(types_.at(&expression)) + "* " + pointer + " = " +
             cType(array) + "_at(" + object + ", " + subscript + ");");
        return pointer;
    }
    default: return {}; // Only checked, mutable targets reach this path.
    }
}

std::string Compiler::emitExpression(const Expression& expression) {
    const Type type = types_.at(&expression);
    switch (expression.kind) {
    case ExpressionKind::Cast: {
        const auto& cast = static_cast<const CastExpression&>(expression);
        const auto value = emitExpression(*cast.value);
        if (type == Type::Any) return value;
        return save(type, "*(" + cType(type) + "*)bc_any_unbox(" + value + ", " +
                          std::to_string(static_cast<int>(type)) + ")");
    }
    case ExpressionKind::Array: {
        const auto& array = static_cast<const ArrayExpression&>(expression);
        const std::string value = save(type, cType(type) + "_new(" +
                                            std::to_string(array.elements.size()) + ")");
        for (std::size_t i = 0; i < array.elements.size(); ++i) {
            const std::string element = emitExpression(*array.elements[i]);
            line(value + ".data[" + std::to_string(i) + "] = " + element + ";");
        }
        return value;
    }
    case ExpressionKind::Literal: {
        const auto& token = static_cast<const LiteralExpression&>(expression).token;
        if (token.type == TokenType::Null) return save(type, "NULL");
        if (type == Type::Bool) return save(type, token.lexeme);
        if (type == Type::String) {
            const std::string value = decodeStringLiteral(token.lexeme);
            std::ostringstream bytes;
            bytes << '"' << std::oct << std::setfill('0');
            for (const unsigned char byte : value) bytes << '\\' << std::setw(3) << unsigned(byte);
            bytes << '"';
            return save(type, "(bc_string){" + bytes.str() + ", " +
                              std::to_string(value.size()) + "}");
        }
        if (type == Type::Float) {
            // Emit the checked binary value exactly. This also avoids C compiler
            // warnings for decimal spellings that underflow to zero.
            std::ostringstream literal;
            literal.imbue(std::locale::classic());
            literal << std::hexfloat << *floatingValue(token.lexeme);
            return save(type, literal.str());
        }
        std::int64_t value = 0;
        std::from_chars(token.lexeme.data(), token.lexeme.data() + token.lexeme.size(), value);
        // Normalize decimal spelling: C would otherwise treat a leading zero as octal.
        return save(type, "INT64_C(" + std::to_string(value) + ")");
    }
    case ExpressionKind::Identifier:
        return save(type, "bc_v_" + static_cast<const IdentifierExpression&>(expression).name);
    case ExpressionKind::Grouping:
        return emitExpression(*static_cast<const GroupingExpression&>(expression).expression);
    case ExpressionKind::Unary: {
        const auto& unary = static_cast<const UnaryExpression&>(expression);
        if (unary.operation.type == TokenType::Ampersand) {
            const std::string address = emitAddress(*unary.operand);
            return save(type, address);
        }
        if (unary.operation.type == TokenType::Star) {
            const std::string pointer = emitAddress(expression);
            return save(type, "*" + pointer);
        }
        if (unary.operation.type == TokenType::Minus &&
            unary.operand->kind == ExpressionKind::Literal &&
            static_cast<const LiteralExpression&>(*unary.operand).token.lexeme ==
                "9223372036854775808") return save(type, "INT64_MIN");
        const std::string operand = emitExpression(*unary.operand);
        if (type == Type::Any) {
            return save(type, "bc_any_unary(" + std::to_string(static_cast<int>(unary.operation.type)) + ", " + operand + ")");
        }
        if (unary.operation.type == TokenType::Minus && type == Type::Int) {
            return save(type, "bc_neg(" + operand + ")");
        }
        return save(type, unary.operation.lexeme + operand);
    }
    case ExpressionKind::Binary: {
        const auto& binary = static_cast<const BinaryExpression&>(expression);
        const std::string left = emitExpression(*binary.left);
        if (binary.operation.type == TokenType::AmpersandAmpersand ||
            binary.operation.type == TokenType::PipePipe) {
            const std::string result = save(type, left);
            const std::string condition = binary.operation.type == TokenType::PipePipe
                ? "!" + result : result;
            line("if (" + condition + ") {");
            ++depth_;
            const std::string right = emitExpression(*binary.right);
            line(result + " = " + right + ";");
            --depth_;
            line("}");
            return result;
        }
        const std::string right = emitExpression(*binary.right);
        if (types_.at(binary.left.get()) == Type::Any) {
            return save(type, type == Type::Bool
                ? comparisonCode(Type::Any, left, binary.operation, right)
                : "bc_any_binary(" + std::to_string(static_cast<int>(binary.operation.type)) + ", " + left + ", " + right + ")");
        }
        if (isAggregate(types_.at(binary.left.get()))) {
            return save(type, comparisonCode(types_.at(binary.left.get()), left, binary.operation, right));
        }
        if (types_.at(binary.left.get()) == Type::String) {
            if (binary.operation.type == TokenType::Plus) {
                return save(type, "bc_concat(" + left + ", " + right + ")");
            }
            return save(type, comparisonCode(Type::String, left, binary.operation, right));
        }
        if (type == Type::Float) {
            if (binary.operation.type == TokenType::Slash) {
                return save(type, "bc_float_div(" + left + ", " + right + ")");
            }
            return save(type, "bc_finite(" + left + " " + binary.operation.lexeme + " " + right + ")");
        }
        std::string helper;
        switch (binary.operation.type) {
        case TokenType::Plus: helper = "bc_add"; break;
        case TokenType::Minus: helper = "bc_sub"; break;
        case TokenType::Star: helper = "bc_mul"; break;
        case TokenType::Slash: helper = "bc_div"; break;
        case TokenType::Percent: helper = "bc_mod"; break;
        default: break;
        }
        if (!helper.empty()) return save(type, helper + "(" + left + ", " + right + ")");
        return save(type, left + " " + binary.operation.lexeme + " " + right);
    }
    case ExpressionKind::SharedComparison: {
        const auto& shared = static_cast<const SharedComparisonExpression&>(expression);
        std::vector<std::string> operands;
        // Snapshot every operand left to right, then evaluate the shared state
        // once. Struct snapshots copy fields; array descriptors still reference
        // shared storage. Joining comparisons cannot skip user side effects.
        for (const auto& operand : shared.operands) operands.push_back(emitExpression(*operand));
        const std::string state = emitExpression(*shared.state);
        if (types_.at(shared.operands.front().get()) == Type::Any) {
            // Check every tag before joining comparisons, even when an early
            // equality would otherwise decide the whole shared comparison.
            for (const auto& operand : operands) line("bc_any_same_type(" + operand + ", " + state + ");");
        }
        std::string comparison;
        for (const auto& operand : operands) {
            if (!comparison.empty()) comparison += shared.grouping.type == TokenType::And ? " && " : " || ";
            comparison += comparisonCode(types_.at(shared.operands.front().get()),
                                         operand, shared.operation, state);
        }
        return save(type, comparison);
    }
    case ExpressionKind::Conditional: {
        const auto& conditional = static_cast<const ConditionalExpression&>(expression);
        const std::string condition = emitExpression(*conditional.condition);
        const std::string result = save(type, (type == Type::String || type == Type::Any || isAggregate(type))
            ? "(" + cType(type) + "){0}" : "0");
        line("if (" + condition + ") {");
        ++depth_;
        const std::string whenTrue = emitExpression(*conditional.whenTrue);
        line(result + " = " + whenTrue + ";");
        --depth_;
        line("} else {");
        ++depth_;
        const std::string whenFalse = emitExpression(*conditional.whenFalse);
        line(result + " = " + whenFalse + ";");
        --depth_;
        line("}");
        return result;
    }
    case ExpressionKind::Assignment: {
        const auto& assignment = static_cast<const AssignmentExpression&>(expression);
        const std::string target = emitAddress(*assignment.target);
        const std::string value = emitExpression(*assignment.value);
        line("*(" + target + ") = " + value + ";");
        return value;
    }
    case ExpressionKind::Call: {
        const auto& call = static_cast<const CallExpression&>(expression);
        const Expression* callee = call.callee.get();
        while (callee->kind == ExpressionKind::Grouping) {
            callee = static_cast<const GroupingExpression*>(callee)->expression.get();
        }
        const auto& name = static_cast<const IdentifierExpression*>(callee)->name;
        const auto function = functions_.find(name);
        const FunctionDeclaration* external = function != functions_.end() &&
            function->second.declaration->external ? function->second.declaration : nullptr;
        std::string arguments;
        for (std::size_t i = 0; i < call.arguments.size(); ++i) {
            std::string value = emitExpression(*call.arguments[i]);
            if (external && external->parameters[i].type.name == "c_int") {
                // Keep the checked value in our normal temporary representation,
                // then explicitly present C's int type to the foreign prototype.
                value = "(int)" + save(Type::Int, "bc_to_c_int(" + value + ")");
            }
            if (!arguments.empty()) arguments += ", ";
            arguments += value;
        }
        if (external) {
            std::string invocation = (external->cppTarget.empty() ? "" : "bc_cpp_") + name + "(" + arguments + ")";
            // Foreign floating-point results re-enter BasicC's finite domain.
            if (type == Type::Float) invocation = "bc_finite(" + invocation + ")";
            if (type != Type::Void) return save(type, invocation);
            line(invocation + ";");
            return {};
        }
        std::string target = "bc_f_" + name;
        if (name == "any") {
            const auto source = types_.at(call.arguments.front().get());
            if (source == Type::Any) return arguments;
            return save(type, "bc_any_box(" + std::to_string(static_cast<int>(source)) +
                              ", sizeof(" + arguments + "), &" + arguments + ")");
        }
        if (name == "type_name") {
            const auto source = types_.at(call.arguments.front().get());
            if (source == Type::Any) return save(type, "bc_any_type_name(" + arguments + ")");
            const auto name = typeName(source);
            return save(type, "(bc_string){\"" + name + "\", " + std::to_string(name.size()) + "}");
        }
        if (namedTypes_.count(name)) return save(type, "(" + cType(type) + "){" + arguments + "}");
        if (conversionType(name)) {
            const Type source = types_.at(call.arguments.front().get());
            if (source == type) return arguments;
            if (type == Type::String) {
                return save(type, stringCode(source, arguments));
            }
            if (source == Type::Any) return save(type, "bc_any_to_" + typeName(type) + "(" + arguments + ")");
            if (source == Type::String) {
                return save(type, "bc_parse_" + std::string(typeName(type)) + "(" + arguments + ")");
            }
            if (type == Type::Int) return save(type, "bc_to_int(" + arguments + ")");
            return save(type, "(double)" + arguments);
        }
        if (name == "input") target = "bc_input";
        if (name == "len") return save(type, types_.at(call.arguments.front().get()) == Type::Any
            ? "bc_any_length(" + arguments + ")" : arguments + ".length");
        if (name == "print") {
            const Type argumentType = types_.at(call.arguments.front().get());
            if (argumentType == Type::Any || isAggregate(argumentType) || isPointer(argumentType)) {
                line("bc_print_string(" + stringCode(argumentType, arguments) + ");");
                return {};
            }
            target = "bc_print_" + typeName(argumentType);
        }
        const std::string invocation = target + "(" + arguments + ")";
        if (type != Type::Void) return save(type, invocation);
        line(invocation + ";");
        return {};
    }
    case ExpressionKind::Index: {
        const auto& index = static_cast<const IndexExpression&>(expression);
        if (types_.at(index.object.get()) == Type::String) {
            const std::string object = emitExpression(*index.object);
            const std::string subscript = emitExpression(*index.index);
            return save(type, "bc_string_at(" + object + ", " + subscript + ")");
        }
        const std::string pointer = emitAddress(expression);
        return save(type, "*" + pointer);
    }
    case ExpressionKind::Member: {
        const auto& member = static_cast<const MemberExpression&>(expression);
        const Type objectType = types_.at(member.object.get());
        const std::string object = emitExpression(*member.object);
        const std::string field = (objectType == Type::String || isArray(objectType))
            ? "length" : "bc_m_" + member.member;
        return save(type, object + "." + field);
    }
    }
    return {};
}

void Compiler::emitBinding(const VariableBinding& binding, bool global) {
    const std::string value = emitExpression(*binding.initializer);
    const std::string prefix = global ? "" : std::string(cType(bindingTypes_.at(&binding))) + " ";
    line(prefix + "bc_v_" + binding.name + " = " + value + ";");
}

void Compiler::emitBlock(const BlockStatement& block) {
    line("{");
    ++depth_;
    for (const auto& statement : block.statements) emitStatement(*statement);
    --depth_;
    line("}");
}

void Compiler::emitStatement(const Statement& statement) {
    switch (statement.kind) {
    case StatementKind::Unsafe:
        // Unsafe changes the checker permissions, not generated evaluation order.
        emitBlock(*static_cast<const UnsafeStatement&>(statement).body);
        break;
    case StatementKind::Block:
        emitBlock(static_cast<const BlockStatement&>(statement));
        break;
    case StatementKind::VariableDeclaration:
        emitBinding(static_cast<const VariableDeclarationStatement&>(statement).binding);
        break;
    case StatementKind::Expression:
        emitExpression(*static_cast<const ExpressionStatement&>(statement).expression);
        break;
    case StatementKind::Return: {
        const auto& returned = static_cast<const ReturnStatement&>(statement);
        const std::string value = returned.value ? emitExpression(*returned.value) : "";
        line("return" + (value.empty() ? "" : " " + value) + ";");
        break;
    }
    case StatementKind::If: {
        const auto& branch = static_cast<const IfStatement&>(statement);
        const std::string condition = emitExpression(*branch.condition);
        line("if (" + condition + ")");
        emitBlock(*branch.thenBranch);
        if (branch.elseBranch) {
            line("else {");
            ++depth_;
            emitStatement(*branch.elseBranch);
            --depth_;
            line("}");
        }
        break;
    }
    case StatementKind::While: {
        const auto& loop = static_cast<const WhileStatement&>(statement);
        line("while (true) {");
        ++depth_;
        const std::string condition = emitExpression(*loop.condition);
        line("if (!" + condition + ") break;");
        emitBlock(*loop.body);
        --depth_;
        line("}");
        break;
    }
    case StatementKind::For: {
        const auto& loop = static_cast<const ForStatement&>(statement);
        // The enclosing block gives the initializer its own scope; condition
        // and increment temporaries are recreated for each iteration.
        line("{");
        ++depth_;
        if (loop.initializer) emitStatement(*loop.initializer);
        line("while (true) {");
        ++depth_;
        if (loop.condition) {
            const std::string condition = emitExpression(*loop.condition);
            line("if (!" + condition + ") break;");
        }
        emitBlock(*loop.body);
        if (loop.increment) emitExpression(*loop.increment);
        --depth_;
        line("}");
        --depth_;
        line("}");
        break;
    }
    }
}

std::string Compiler::signature(const FunctionDeclaration& function) {
    const std::string resultType = function.external ? foreignCType(*function.returnType)
        : cType(*functions_.at(function.name).result);
    std::string result = (function.external ? "extern " : "") + resultType + " " +
                        (function.external ? (function.cppTarget.empty() ? "" : "bc_cpp_") : "bc_f_") + function.name + "(";
    for (const auto& parameter : function.parameters) {
        if (result.back() != '(') result += ", ";
        const std::string parameterType = function.external ? foreignCType(parameter.type)
            : cType(resolveType(parameter.type));
        result += parameterType + " bc_v_" + parameter.name;
    }
    if (function.parameters.empty()) result += "void";
    return result + ")";
}

std::string Compiler::generateCpp() {
    std::string result;
    for (const auto& declaration : program_.declarations) {
        if (declaration->kind != DeclarationKind::Function) continue;
        const auto& function = static_cast<const FunctionDeclaration&>(*declaration);
        if (function.cppTarget.empty()) continue;
        if (result.empty()) result = "#include <stdint.h>\n#include <exception>\n#include <cstdio>\n#include <cstdlib>\n\n";
        std::string parameters, arguments;
        for (const auto& parameter : function.parameters) {
            if (!parameters.empty()) { parameters += ", "; arguments += ", "; }
            parameters += foreignCType(parameter.type);
            arguments += "bc_v_" + parameter.name;
        }
        // Exact function-pointer selection resolves overloads and rejects ABI
        // mismatches. Catch exceptions before they cross the generated C frame.
        result += "extern \"C\" " + signature(function).substr(7) + " noexcept {\n    try {\n";
        result += "        using Target = " + foreignCType(*function.returnType) + " (*)(" + parameters + ");\n";
        const auto target = function.cppTarget.rfind("::", 0) == 0
            ? function.cppTarget : "::" + function.cppTarget;
        result += "        auto target = static_cast<Target>(&" + target + ");\n";
        result += "        return target(" + arguments + ");\n";
        result += "    } catch (const std::exception& error) {\n"
                  "        std::fprintf(stderr, \"BasicC C++ exception: %s\\n\", error.what());\n"
                  "        std::exit(1);\n"
                  "    } catch (...) {\n"
                  "        std::fputs(\"BasicC C++ exception\\n\", stderr);\n"
                  "        std::exit(1);\n"
                  "    }\n}\n\n";
    }
    return result;
}

void Compiler::emitAggregateTypes() {
    // Dependencies precede their containing structs/arrays. Recursive value
    // types were rejected during resolution, so C definitions remain finite.
    for (const Type type : aggregateOrder_) {
        const auto& info = aggregate(type);
        const std::string name = cType(type);
        line("typedef struct {");
        ++depth_;
        if (info.element) {
            line(cType(*info.element) + "* data;");
            line("int64_t length;");
        } else {
            for (std::size_t i = 0; i < info.fields.size(); ++i) {
                line(cType(info.fields[i]) + " bc_m_" + info.structure->fields[i].name + ";");
            }
        }
        --depth_;
        line("} " + name + ";");
        if (info.element) {
            const std::string element = cType(*info.element);
            line("static " + name + " " + name + "_new(int64_t length) {");
            ++depth_;
            line("if (length < 0 || (uint64_t)length > SIZE_MAX / sizeof(" + element + "))");
            line("    bc_fail(\"array is too large\");");
            line("return (" + name + "){(" + element + "*)bc_allocate((size_t)length * sizeof(" + element + ")), length};");
            --depth_;
            line("}");
            line("static " + element + "* " + name + "_at(" + name + " value, int64_t index) {");
            ++depth_;
            line("if (index < 0 || index >= value.length) bc_fail(\"array index out of bounds\");");
            line("return &value.data[index];");
            --depth_;
            line("}");
        }
        line("static bool " + name + "_equal(" + name + " a, " + name + " b) {");
        ++depth_;
        const Token equal{TokenType::EqualEqual, "==", {1, 1}};
        if (info.element) {
            line("if (a.length != b.length) return false;");
            line("for (int64_t i = 0; i < a.length; ++i) {");
            ++depth_;
            line("if (!(" + comparisonCode(*info.element, "a.data[i]", equal, "b.data[i]") + ")) return false;");
            --depth_;
            line("}");
        } else {
            for (std::size_t i = 0; i < info.fields.size(); ++i) {
                const std::string field = ".bc_m_" + info.structure->fields[i].name;
                line("if (!(" + comparisonCode(info.fields[i], "a" + field, equal, "b" + field) + ")) return false;");
            }
        }
        line("return true;");
        --depth_;
        line("}");
        line("static bc_string " + name + "_string(" + name + " value) {");
        ++depth_;
        const std::string opening = info.element ? "[" : info.name + "(";
        line("bc_string result = {\"" + opening + "\", " + std::to_string(opening.size()) + "};");
        if (info.element) {
            line("for (int64_t i = 0; i < value.length; ++i) {");
            ++depth_;
            line("if (i) result = bc_concat(result, (bc_string){\", \", 2});");
            line("result = bc_concat(result, " + stringCode(*info.element, "value.data[i]") + ");");
            --depth_;
            line("}");
        } else {
            for (std::size_t i = 0; i < info.fields.size(); ++i) {
                const std::string field = info.structure->fields[i].name;
                const std::string label = (i ? ", " : "") + field + ": ";
                line("result = bc_concat(result, (bc_string){\"" + label + "\", " + std::to_string(label.size()) + "});");
                line("result = bc_concat(result, " + stringCode(info.fields[i], "value.bc_m_" + field) + ");");
            }
        }
        line(std::string("return bc_concat(result, (bc_string){\"") + (info.element ? "]" : ")") + "\", 1});");
        --depth_;
        line("}");
    }
}

void Compiler::emitAnyRuntime() {
    // Tags reuse the checked type registry, so nominal structs and nested array
    // types retain their identity across boxing. Boxes never contain pointers.
    std::vector<Type> payloadTypes{Type::Int, Type::Float, Type::Bool, Type::String};
    payloadTypes.insert(payloadTypes.end(), aggregateOrder_.begin(), aggregateOrder_.end());
    const auto tag = [](Type type) { return std::to_string(static_cast<int>(type)); };
    const auto operation = [](TokenType type) { return std::to_string(static_cast<int>(type)); };
    const Token equal{TokenType::EqualEqual, "==", {1, 1}};

    line("static bc_string bc_any_type_name(bc_any value) {");
    line("    switch (value.tag) {");
    for (const auto type : payloadTypes) {
        const auto name = typeName(type);
        line("    case " + tag(type) + ": return (bc_string){\"" + name + "\", " + std::to_string(name.size()) + "};");
    }
    line("    default: bc_fail(\"invalid any type tag\");");
    line("    }");
    line("}");

    line("static bool bc_any_equal(bc_any a, bc_any b) {");
    line("    if (a.tag != b.tag) return false;");
    line("    static unsigned depth;");
    line("    if (++depth > 128) bc_fail(\"any equality is cyclic or too deeply nested\");");
    line("    bool result = false;");
    line("    switch (a.tag) {");
    for (const auto type : payloadTypes) {
        const auto left = "(*(" + cType(type) + "*)a.value)";
        const auto right = "(*(" + cType(type) + "*)b.value)";
        line("    case " + tag(type) + ": result = " + comparisonCode(type, left, equal, right) + "; break;");
    }
    line("    default: bc_fail(\"invalid any type tag\");");
    line("    }");
    line("    --depth;");
    line("    return result;");
    line("}");

    line("static bc_string bc_any_string(bc_any value) {");
    line("    static unsigned depth;");
    line("    if (++depth > 128) bc_fail(\"any formatting is cyclic or too deeply nested\");");
    line("    bc_string result = {0};");
    line("    switch (value.tag) {");
    for (const auto type : payloadTypes) {
        line("    case " + tag(type) + ": result = " + stringCode(type, "(*(" + cType(type) + "*)value.value)") + "; break;");
    }
    line("    default: bc_fail(\"invalid any type tag\");");
    line("    }");
    line("    --depth;");
    line("    return result;");
    line("}");

    line("static int64_t bc_any_length(bc_any value) {");
    line("    switch (value.tag) {");
    for (const auto type : payloadTypes) {
        if (type == Type::String || isArray(type)) {
            line("    case " + tag(type) + ": return ((" + cType(type) + "*)value.value)->length;");
        }
    }
    line("    default: bc_fail(\"len requires an any containing a string or array\");");
    line("    }");
    line("}");

    for (const auto target : {Type::Int, Type::Float}) {
        line("static " + cType(target) + " bc_any_to_" + typeName(target) + "(bc_any value) {");
        line("    switch (value.tag) {");
        for (const auto source : {Type::Int, Type::Float, Type::String}) {
            std::string value = "(*(" + cType(source) + "*)value.value)";
            if (source == Type::String) value = "bc_parse_" + typeName(target) + "(" + value + ")";
            else if (source != target) value = target == Type::Int ? "bc_to_int(" + value + ")" : "(double)" + value;
            line("    case " + tag(source) + ": return " + value + ";");
        }
        line("    default: bc_fail(\"numeric conversion requires an any containing int, float, or string\");");
        line("    }");
        line("}");
    }

    line("static bool bc_any_compare(int operation, bc_any a, bc_any b) {");
    line("    bc_any_same_type(a, b);");
    for (const auto type : {Type::Int, Type::Float}) {
        line("    if (a.tag == " + tag(type) + ") {");
        line("        " + cType(type) + " left = *(" + cType(type) + "*)a.value, right = *(" + cType(type) + "*)b.value;");
        line("        switch (operation) {");
        for (const auto& op : std::vector<std::pair<TokenType, std::string>>{
                 {TokenType::Less, "<"}, {TokenType::LessEqual, "<="},
                 {TokenType::Greater, ">"}, {TokenType::GreaterEqual, ">="}}) {
            line("        case " + operation(op.first) + ": return left " + op.second + " right;");
        }
        line("        }");
        line("    }");
    }
    line("    bc_fail(\"ordering requires any values containing the same numeric type\");");
    line("}");

    line("static bc_any bc_any_binary(int operation, bc_any a, bc_any b) {");
    line("    bc_any_same_type(a, b);");
    for (const auto type : {Type::Int, Type::Float, Type::String}) {
        line("    if (a.tag == " + tag(type) + ") {");
        line("        " + cType(type) + " left = *(" + cType(type) + "*)a.value, right = *(" + cType(type) + "*)b.value;");
        line("        " + cType(type) + " result;");
        line("        switch (operation) {");
        for (const auto& op : std::vector<std::pair<TokenType, std::string>>{
                 {TokenType::Plus, "+"}, {TokenType::Minus, "-"}, {TokenType::Star, "*"},
                 {TokenType::Slash, "/"}, {TokenType::Percent, "%"}, {TokenType::Ampersand, "&"}, {TokenType::Pipe, "|"}}) {
            if (type == Type::String && op.first != TokenType::Plus) continue;
            if (type == Type::Float && (op.first == TokenType::Percent || op.first == TokenType::Ampersand || op.first == TokenType::Pipe)) continue;
            std::string expression;
            if (type == Type::String) expression = "bc_concat(left, right)";
            else if (type == Type::Float) expression = op.first == TokenType::Slash
                ? "bc_float_div(left, right)" : "bc_finite(left " + op.second + " right)";
            else {
                std::string helper;
                switch (op.first) {
                case TokenType::Plus: helper = "bc_add"; break;
                case TokenType::Minus: helper = "bc_sub"; break;
                case TokenType::Star: helper = "bc_mul"; break;
                case TokenType::Slash: helper = "bc_div"; break;
                case TokenType::Percent: helper = "bc_mod"; break;
                default: break;
                }
                expression = helper.empty() ? "left " + op.second + " right" : helper + "(left, right)";
            }
            line("        case " + operation(op.first) + ": result = " + expression + "; break;");
        }
        line("        default: bc_fail(\"operator does not support this any payload type\");");
        line("        }");
        line("        return bc_any_box(a.tag, sizeof result, &result);");
        line("    }");
    }
    line("    bc_fail(\"arithmetic requires numeric any values; '+' also concatenates strings\");");
    line("}");

    line("static bc_any bc_any_unary(int operation, bc_any value) {");
    for (const auto type : {Type::Int, Type::Float}) {
        line("    if (value.tag == " + tag(type) + ") {");
        line("        " + cType(type) + " result = *(" + cType(type) + "*)value.value;");
        line("        if (operation == " + operation(TokenType::Minus) + ") result = " +
             (type == Type::Int ? "bc_neg(result)" : "-result") + ";");
        line("        return bc_any_box(value.tag, sizeof result, &result);");
        line("    }");
    }
    line("    bc_fail(\"unary '+' and '-' require a numeric any value\");");
    line("}");
}

std::string Compiler::generate() {
    // The small runtime makes signed arithmetic defined: overflow and invalid
    // division stop the program instead of inheriting C's undefined behavior.
    output_ = R"C(/* Generated by BasicC. User expressions are evaluated left to right. */
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <limits.h>

_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "BasicC float requires binary64 double");

_Noreturn static void bc_fail(const char* message) {
    fprintf(stderr, "BasicC runtime error: %s\n", message);
    exit(1);
}
static int64_t bc_add(int64_t a, int64_t b) {
    int64_t result;
    if (__builtin_add_overflow(a, b, &result)) bc_fail("integer overflow");
    return result;
}
static int64_t bc_sub(int64_t a, int64_t b) {
    int64_t result;
    if (__builtin_sub_overflow(a, b, &result)) bc_fail("integer overflow");
    return result;
}
static int64_t bc_mul(int64_t a, int64_t b) {
    int64_t result;
    if (__builtin_mul_overflow(a, b, &result)) bc_fail("integer overflow");
    return result;
}
static int64_t bc_neg(int64_t value) { return bc_sub(0, value); }
static int bc_to_c_int(int64_t value) {
    if (value < INT_MIN || value > INT_MAX) bc_fail("value is outside the C int range");
    return (int)value;
}
static int64_t bc_div(int64_t a, int64_t b) {
    if (b == 0) bc_fail("division by zero");
    if (a == INT64_MIN && b == -1) bc_fail("integer overflow");
    return a / b;
}
static int64_t bc_mod(int64_t a, int64_t b) {
    if (b == 0) bc_fail("remainder by zero");
    if (a == INT64_MIN && b == -1) return 0;
    return a % b;
}
static void bc_print_int(int64_t value) { printf("%" PRId64 "\n", value); }
static void bc_print_bool(bool value) { puts(value ? "true" : "false"); }
static void bc_print_float(double value) { printf("%.17g\n", value); }
static double bc_finite(double value) {
    if (!isfinite(value)) bc_fail("non-finite floating-point result");
    return value;
}
static double bc_float_div(double a, double b) {
    if (b == 0.0) bc_fail("division by zero");
    return bc_finite(a / b);
}
static int64_t bc_to_int(double value) {
    /* The upper bound is exclusive: (double)INT64_MAX rounds up to 2^63.
       Checking before the cast prevents undefined behavior in generated C. */
    if (!isfinite(value) || value < -0x1p63 || value >= 0x1p63)
        bc_fail("float-to-int conversion is outside the signed 64-bit range");
    return (int64_t)value;
}

/* Strings are immutable byte sequences. Copies share storage safely because
   runtime-owned bytes live until program exit; no reference counting is needed
   for this first string runtime. The explicit length preserves embedded NULs. */
typedef struct { const char* data; int64_t length; } bc_string;
typedef struct bc_allocation {
    struct bc_allocation* next;
    /* The arena holds array elements, boxed values, and string bytes. */
    _Alignas(max_align_t) char data[];
} bc_allocation;
static bc_allocation* bc_allocations;
static void bc_cleanup(void) {
    while (bc_allocations) {
        bc_allocation* next = bc_allocations->next;
        free(bc_allocations);
        bc_allocations = next;
    }
}
static char* bc_allocate(size_t size) {
    if (size > SIZE_MAX - sizeof(bc_allocation)) bc_fail("allocation is too large");
    bc_allocation* block = malloc(sizeof(bc_allocation) + size);
    if (!block) bc_fail("out of memory");
    block->next = bc_allocations;
    bc_allocations = block;
    return block->data;
}
static bc_string bc_string_copy(const char* data, size_t length) {
    if (length > INT64_MAX || length == SIZE_MAX) bc_fail("string is too large");
    char* copy = bc_allocate(length + 1);
    memcpy(copy, data, length);
    copy[length] = '\0';
    return (bc_string){copy, (int64_t)length};
}
static bc_string bc_string_at(bc_string value, int64_t index) {
    if (index < 0 || index >= value.length) bc_fail("string index out of bounds");
    /* Copy to keep the numeric-conversion contract of NUL-terminated storage. */
    return bc_string_copy(value.data + index, 1);
}
static bc_string bc_concat(bc_string left, bc_string right) {
    int64_t length = bc_add(left.length, right.length);
    if ((uint64_t)length >= SIZE_MAX) bc_fail("string is too large");
    char* data = bc_allocate((size_t)length + 1);
    memcpy(data, left.data, (size_t)left.length);
    memcpy(data + left.length, right.data, (size_t)right.length);
    data[length] = '\0';
    return (bc_string){data, length};
}
static bool bc_string_equal(bc_string a, bc_string b) {
    return a.length == b.length && memcmp(a.data, b.data, (size_t)a.length) == 0;
}
static void bc_print_string(bc_string value) {
    if (fwrite(value.data, 1, (size_t)value.length, stdout) != (size_t)value.length ||
        putchar('\n') == EOF) bc_fail("could not write output");
}
static bc_string bc_string_from_int(int64_t value) {
    char buffer[32];
    int length = snprintf(buffer, sizeof buffer, "%" PRId64, value);
    return bc_string_copy(buffer, (size_t)length);
}
static bc_string bc_string_from_float(double value) {
    char buffer[32];
    int length = snprintf(buffer, sizeof buffer, "%.17g", value);
    return bc_string_copy(buffer, (size_t)length);
}
static bc_string bc_string_from_bool(bool value) {
    return value ? (bc_string){"true", 4} : (bc_string){"false", 5};
}
static bc_string bc_string_from_pointer(const void* value) {
    char buffer[2 * sizeof(void*) + 16];
    int length = snprintf(buffer, sizeof buffer, "%p", value);
    return bc_string_copy(buffer, (size_t)length);
}
static bool bc_digit(char byte) { return byte >= '0' && byte <= '9'; }
static void bc_check_number(bc_string text, bool floating) {
    /* Validate decimal syntax before calling libc: accept no whitespace,
       embedded NUL, hexadecimal spelling, infinity, or trailing garbage. */
    int64_t i = 0;
    if (i < text.length && (text.data[i] == '+' || text.data[i] == '-')) ++i;
    int64_t start = i;
    while (i < text.length && bc_digit(text.data[i])) ++i;
    bool digits = i > start;
    if (floating && i < text.length && text.data[i] == '.') {
        start = ++i;
        while (i < text.length && bc_digit(text.data[i])) ++i;
        digits = digits || i > start;
    }
    if (!digits) bc_fail("invalid numeric string");
    if (floating && i < text.length && (text.data[i] == 'e' || text.data[i] == 'E')) {
        ++i;
        if (i < text.length && (text.data[i] == '+' || text.data[i] == '-')) ++i;
        start = i;
        while (i < text.length && bc_digit(text.data[i])) ++i;
        if (i == start) bc_fail("invalid numeric string");
    }
    if (i != text.length) bc_fail("invalid numeric string");
}
static int64_t bc_parse_int(bc_string value) {
    bc_check_number(value, false);
    errno = 0;
    intmax_t parsed = strtoimax(value.data, NULL, 10);
    if (errno == ERANGE || parsed < INT64_MIN || parsed > INT64_MAX)
        bc_fail("integer string is outside the signed 64-bit range");
    return (int64_t)parsed;
}
static double bc_parse_float(bc_string value) {
    bc_check_number(value, true);
    return bc_finite(strtod(value.data, NULL));
}
static bc_string bc_input(void) {
    size_t capacity = 64, length = 0;
    char* buffer = malloc(capacity);
    if (!buffer) bc_fail("out of memory");
    fflush(stdout);
    int byte;
    while ((byte = getchar()) != EOF && byte != '\n') {
        if (length == capacity) {
            if (capacity > SIZE_MAX / 2) bc_fail("input is too large");
            capacity *= 2;
            char* grown = realloc(buffer, capacity);
            if (!grown) bc_fail("out of memory");
            buffer = grown;
        }
        buffer[length++] = (char)byte;
    }
    if (ferror(stdin)) bc_fail("could not read input");
    if (byte == '\n' && length && buffer[length - 1] == '\r') --length;
    bc_string result = bc_string_copy(buffer, length);
    free(buffer);
    return result;
}

/* Boxes own immutable value copies in the arena. Arrays inside a box retain
   their usual shared storage. Raw pointers cannot enter boxes, so type erasure
   cannot bypass the static borrow checker. */
typedef struct { int tag; void* value; } bc_any;
static bc_any bc_any_box(int tag, size_t size, const void* value) {
    void* copy = bc_allocate(size);
    memcpy(copy, value, size);
    return (bc_any){tag, copy};
}
static void* bc_any_unbox(bc_any value, int tag) {
    if (value.tag != tag) bc_fail("any cast type mismatch");
    return value.value;
}
static void bc_any_same_type(bc_any a, bc_any b) {
    if (a.tag != b.tag) bc_fail("any operands must contain the same type");
}
/* Forward declarations allow aggregate helpers to visit any fields, while
   the dispatch definitions below can refer to every concrete aggregate type. */
static bool bc_any_equal(bc_any a, bc_any b);
static bc_string bc_any_string(bc_any value);

)C";
    emitAggregateTypes();
    emitAnyRuntime();
    for (const auto& declaration : program_.declarations) {
        if (declaration->kind == DeclarationKind::Function) {
            line(signature(static_cast<const FunctionDeclaration&>(*declaration)) + ";");
        } else if (declaration->kind == DeclarationKind::GlobalVariable) {
            const auto& binding = static_cast<const GlobalVariableDeclaration&>(*declaration).binding;
            line(std::string(cType(bindingTypes_.at(&binding))) + " bc_v_" + binding.name + ";");
        }
    }
    for (const auto& declaration : program_.declarations) {
        if (declaration->kind != DeclarationKind::Function) continue;
        const auto& function = static_cast<const FunctionDeclaration&>(*declaration);
        if (function.external) continue;
        line(signature(function));
        emitBlock(*function.body);
    }
    line("int main(void) {");
    ++depth_;
    line("if (atexit(bc_cleanup) != 0) bc_fail(\"could not register runtime cleanup\");");
    for (const auto& declaration : program_.declarations) {
        if (declaration->kind == DeclarationKind::GlobalVariable) {
            emitBinding(static_cast<const GlobalVariableDeclaration&>(*declaration).binding, true);
        }
    }
    // A process status has a deliberately small, portable range.
    line("return (int)((uint64_t)bc_f_main() & UINT64_C(255));");
    --depth_;
    line("}");
    return std::move(output_);
}

CompileResult Compiler::run() {
    try {
        for (const auto& declaration : program_.declarations) {
            if (declaration->kind == DeclarationKind::Import) {
                fail(declaration->location, "imports must be loaded before compilation");
            }
            if (declaration->kind != DeclarationKind::Struct) continue;
            const auto& structure = static_cast<const StructDeclaration&>(*declaration);
            const Type type = static_cast<Type>(static_cast<std::size_t>(Type::AggregateStart) + aggregateTypes_.size());
            if (isBuiltinFunction(structure.name) || structure.name == "void" || structure.name == "c_int" ||
                !namedTypes_.emplace(structure.name, type).second) {
                fail(structure.location, "duplicate or reserved type '" + structure.name + "'");
            }
            aggregateTypes_.push_back({structure.name, &structure, std::nullopt, {}, Visit::Unchecked});
        }
        for (const auto& declaration : program_.declarations) {
            if (declaration->kind == DeclarationKind::Struct) {
                resolveStruct(namedTypes_.at(static_cast<const StructDeclaration&>(*declaration).name));
            }
        }
        // Register signatures first to support forward calls and explicitly
        // typed recursion. Global initializers run in declaration order.
        for (const auto& declaration : program_.declarations) {
            if (declaration->kind != DeclarationKind::Function) continue;
            const auto& function = static_cast<const FunctionDeclaration&>(*declaration);
            std::optional<Type> type;
            if (function.external && (function.name == "main" || function.name.rfind("bc_", 0) == 0)) {
                fail(function.location, "extern symbol uses a reserved runtime name");
            }
            if (function.returnType) type = function.external
                ? foreignType(*function.returnType, true) : resolveType(*function.returnType, true);
            if (isBuiltinFunction(function.name) || namedTypes_.count(function.name) ||
                !functions_.emplace(function.name, FunctionInfo{&function, type}).second) {
                fail(function.location, "duplicate or reserved function '" + function.name + "'");
            }
        }
        globalInitializer_ = true;
        for (const auto& declaration : program_.declarations) {
            if (declaration->kind == DeclarationKind::GlobalVariable) {
                checkBinding(static_cast<const GlobalVariableDeclaration&>(*declaration).binding);
            }
        }
        globalInitializer_ = false;
        for (const auto& declaration : program_.declarations) {
            if (declaration->kind == DeclarationKind::Function) {
                checkFunction(functions_.at(static_cast<const FunctionDeclaration&>(*declaration).name));
            }
        }
        const auto main = functions_.find("main");
        if (main == functions_.end()) fail({1, 1}, "an executable needs fn main() -> int");
        if (!main->second.declaration->parameters.empty() || main->second.result != Type::Int) {
            fail(main->second.declaration->location, "main must take no parameters and return int");
        }
        if (const auto diagnostic = lifetimes_.check()) {
            fail(diagnostic->location, diagnostic->message);
        }
        result_.cSource = generate();
        result_.cppSource = generateCpp();
    } catch (const SemanticError&) {
        // Stop at the first semantic error; no partial C is exposed or compiled.
    }
    return std::move(result_);
}

} // namespace

CompileResult compile(const Program& program) {
    return Compiler(program).run();
}

} // namespace basicc
