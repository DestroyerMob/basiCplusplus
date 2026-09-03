#pragma once

#include "ast.hpp"

#include <string>

namespace basicc {

class AstPrinter {
public:
    [[nodiscard]] std::string print(const Program& program);

private:
    void printDeclaration(const Declaration& declaration, std::size_t depth);
    void printBinding(const VariableBinding& binding, std::size_t depth);
    void printStatement(const Statement& statement, std::size_t depth);
    void printExpression(const Expression& expression, std::size_t depth);
    void printType(const TypeSyntax& type, std::size_t depth,
                   std::string_view label);
    void writeLine(std::size_t depth, std::string_view text);

    std::string output_;
};

} // namespace basicc
