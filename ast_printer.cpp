#include "ast_printer.hpp"

#include <string_view>

namespace basicc {
namespace {

std::string canonicalTypeName(const TypeSyntax& type) {
    std::string name = type.name;
    switch (type.kind) {
    case TypeKind::Int: name = "int"; break;
    case TypeKind::Float: name = "float"; break;
    case TypeKind::Bool: name = "bool"; break;
    case TypeKind::String: name = "string"; break;
    case TypeKind::Named: break;
    }
    for (const auto suffix : type.suffixes) name += suffix == TypeSuffix::Array ? "[]" : "*";
    return name;
}

} // namespace

std::string AstPrinter::print(const Program& program) {
    output_.clear();
    writeLine(0, "Program");
    for (const DeclarationPtr& declaration : program.declarations) {
        printDeclaration(*declaration, 1);
    }
    return output_;
}

void AstPrinter::printDeclaration(const Declaration& declaration,
                                  std::size_t depth) {
    switch (declaration.kind) {
    case DeclarationKind::Import:
        writeLine(depth, "Import " + static_cast<const ImportDeclaration&>(declaration).path.lexeme);
        return;
    case DeclarationKind::Struct: {
        const auto& structure = static_cast<const StructDeclaration&>(declaration);
        writeLine(depth, "Struct " + structure.name);
        for (const auto& field : structure.fields) {
            writeLine(depth + 1, "Field " + field.name + ": " + canonicalTypeName(field.type));
        }
        return;
    }
    case DeclarationKind::Function: {
        const auto& function = static_cast<const FunctionDeclaration&>(declaration);
        writeLine(depth, std::string(function.external ? "ExternFunction " : "Function ") + function.name);
        if (!function.cppTarget.empty()) writeLine(depth + 1, "C++ target " + function.cppTarget);
        writeLine(depth + 1, "Parameters");
        for (const Parameter& parameter : function.parameters) {
            writeLine(depth + 2,
                      "Parameter " + parameter.name + ": " +
                          std::string(canonicalTypeName(parameter.type)));
        }
        if (function.returnType.has_value()) {
            printType(*function.returnType, depth + 1, "Returns");
        } else {
            writeLine(depth + 1, "Returns <inferred>");
        }
        if (function.body) printStatement(*function.body, depth + 1);
        return;
    }
    case DeclarationKind::GlobalVariable: {
        const auto& variable =
            static_cast<const GlobalVariableDeclaration&>(declaration);
        writeLine(depth, "GlobalVariable");
        printBinding(variable.binding, depth + 1);
        return;
    }
    }
}

void AstPrinter::printBinding(const VariableBinding& binding, std::size_t depth) {
    const std::string typeName = binding.explicitType.has_value()
                                     ? std::string(canonicalTypeName(*binding.explicitType))
                                     : "<inferred>";
    writeLine(depth, "Binding " + binding.name + ": " + typeName);
    writeLine(depth + 1, "Initializer");
    printExpression(*binding.initializer, depth + 2);
}

void AstPrinter::printStatement(const Statement& statement, std::size_t depth) {
    switch (statement.kind) {
    case StatementKind::Unsafe:
        writeLine(depth, "Unsafe");
        printStatement(*static_cast<const UnsafeStatement&>(statement).body, depth + 1);
        return;
    case StatementKind::Block: {
        const auto& block = static_cast<const BlockStatement&>(statement);
        writeLine(depth, "Block");
        for (const StatementPtr& child : block.statements) {
            printStatement(*child, depth + 1);
        }
        return;
    }
    case StatementKind::VariableDeclaration: {
        const auto& variable =
            static_cast<const VariableDeclarationStatement&>(statement);
        writeLine(depth, "Variable");
        printBinding(variable.binding, depth + 1);
        return;
    }
    case StatementKind::Expression: {
        const auto& expression = static_cast<const ExpressionStatement&>(statement);
        writeLine(depth, "ExpressionStatement");
        printExpression(*expression.expression, depth + 1);
        return;
    }
    case StatementKind::Return: {
        const auto& returnStatement = static_cast<const ReturnStatement&>(statement);
        writeLine(depth, "Return");
        if (returnStatement.value) {
            printExpression(*returnStatement.value, depth + 1);
        } else {
            writeLine(depth + 1, "<empty>");
        }
        return;
    }
    case StatementKind::If: {
        const auto& ifStatement = static_cast<const IfStatement&>(statement);
        writeLine(depth, "If");
        writeLine(depth + 1, "Condition");
        printExpression(*ifStatement.condition, depth + 2);
        writeLine(depth + 1, "Then");
        printStatement(*ifStatement.thenBranch, depth + 2);
        if (ifStatement.elseBranch) {
            writeLine(depth + 1, "Else");
            printStatement(*ifStatement.elseBranch, depth + 2);
        }
        return;
    }
    case StatementKind::While: {
        const auto& whileStatement = static_cast<const WhileStatement&>(statement);
        writeLine(depth, "While");
        writeLine(depth + 1, "Condition");
        printExpression(*whileStatement.condition, depth + 2);
        writeLine(depth + 1, "Body");
        printStatement(*whileStatement.body, depth + 2);
        return;
    }
    case StatementKind::For: {
        const auto& forStatement = static_cast<const ForStatement&>(statement);
        writeLine(depth, "For");
        writeLine(depth + 1, "Initializer");
        if (forStatement.initializer) {
            printStatement(*forStatement.initializer, depth + 2);
        } else {
            writeLine(depth + 2, "<empty>");
        }
        writeLine(depth + 1, "Condition");
        if (forStatement.condition) {
            printExpression(*forStatement.condition, depth + 2);
        } else {
            writeLine(depth + 2, "<empty>");
        }
        writeLine(depth + 1, "Increment");
        if (forStatement.increment) {
            printExpression(*forStatement.increment, depth + 2);
        } else {
            writeLine(depth + 2, "<empty>");
        }
        writeLine(depth + 1, "Body");
        printStatement(*forStatement.body, depth + 2);
        return;
    }
    }
}

void AstPrinter::printExpression(const Expression& expression,
                                 std::size_t depth) {
    switch (expression.kind) {
    case ExpressionKind::Array: {
        writeLine(depth, "Array");
        for (const auto& element : static_cast<const ArrayExpression&>(expression).elements) {
            printExpression(*element, depth + 1);
        }
        return;
    }
    case ExpressionKind::Literal: {
        const auto& literal = static_cast<const LiteralExpression&>(expression);
        writeLine(depth,
                  std::string(tokenTypeName(literal.token.type)) + " " +
                      literal.token.lexeme);
        return;
    }
    case ExpressionKind::Identifier: {
        const auto& identifier =
            static_cast<const IdentifierExpression&>(expression);
        writeLine(depth, "Identifier " + identifier.name);
        return;
    }
    case ExpressionKind::Grouping: {
        const auto& grouping = static_cast<const GroupingExpression&>(expression);
        writeLine(depth, "Grouping");
        printExpression(*grouping.expression, depth + 1);
        return;
    }
    case ExpressionKind::Unary: {
        const auto& unary = static_cast<const UnaryExpression&>(expression);
        writeLine(depth, "Unary " + unary.operation.lexeme);
        printExpression(*unary.operand, depth + 1);
        return;
    }
    case ExpressionKind::Binary: {
        const auto& binary = static_cast<const BinaryExpression&>(expression);
        writeLine(depth, "Binary " + binary.operation.lexeme);
        printExpression(*binary.left, depth + 1);
        printExpression(*binary.right, depth + 1);
        return;
    }
    case ExpressionKind::SharedComparison: {
        const auto& comparison =
            static_cast<const SharedComparisonExpression&>(expression);
        writeLine(depth, "SharedComparison " + comparison.grouping.lexeme + " " +
                             comparison.operation.lexeme);
        writeLine(depth + 1, "Operands");
        for (const ExpressionPtr& operand : comparison.operands) {
            printExpression(*operand, depth + 2);
        }
        writeLine(depth + 1, "State");
        printExpression(*comparison.state, depth + 2);
        return;
    }
    case ExpressionKind::Conditional: {
        const auto& conditional =
            static_cast<const ConditionalExpression&>(expression);
        writeLine(depth, "Conditional");
        writeLine(depth + 1, "Condition");
        printExpression(*conditional.condition, depth + 2);
        writeLine(depth + 1, "WhenTrue");
        printExpression(*conditional.whenTrue, depth + 2);
        writeLine(depth + 1, "WhenFalse");
        printExpression(*conditional.whenFalse, depth + 2);
        return;
    }
    case ExpressionKind::Assignment: {
        const auto& assignment =
            static_cast<const AssignmentExpression&>(expression);
        writeLine(depth, "Assignment");
        writeLine(depth + 1, "Target");
        printExpression(*assignment.target, depth + 2);
        writeLine(depth + 1, "Value");
        printExpression(*assignment.value, depth + 2);
        return;
    }
    case ExpressionKind::Cast: {
        const auto& cast = static_cast<const CastExpression&>(expression);
        writeLine(depth, "Cast " + canonicalTypeName(cast.target));
        printExpression(*cast.value, depth + 1);
        return;
    }
    case ExpressionKind::Call: {
        const auto& call = static_cast<const CallExpression&>(expression);
        writeLine(depth, "Call");
        writeLine(depth + 1, "Callee");
        printExpression(*call.callee, depth + 2);
        writeLine(depth + 1, "Arguments");
        for (const ExpressionPtr& argument : call.arguments) {
            printExpression(*argument, depth + 2);
        }
        return;
    }
    case ExpressionKind::Index: {
        const auto& index = static_cast<const IndexExpression&>(expression);
        writeLine(depth, "Index");
        writeLine(depth + 1, "Object");
        printExpression(*index.object, depth + 2);
        writeLine(depth + 1, "Subscript");
        printExpression(*index.index, depth + 2);
        return;
    }
    case ExpressionKind::Member: {
        const auto& member = static_cast<const MemberExpression&>(expression);
        writeLine(depth, "Member " + member.member);
        printExpression(*member.object, depth + 1);
        return;
    }
    }
}

void AstPrinter::printType(const TypeSyntax& type, std::size_t depth,
                           std::string_view label) {
    writeLine(depth,
              std::string(label) + " " + std::string(canonicalTypeName(type)));
}

void AstPrinter::writeLine(std::size_t depth, std::string_view text) {
    output_.append(depth * 2, ' ');
    output_.append(text);
    output_.push_back('\n');
}

} // namespace basicc
