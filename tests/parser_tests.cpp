#include "../ast_printer.hpp"
#include "../lexer.hpp"
#include "../parser.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

basicc::ParseResult parseSource(std::string_view source) {
    basicc::Lexer lexer(source);
    basicc::LexResult lexResult = lexer.lex();
    check(lexResult.ok(), "parser test source should pass lexing");
    basicc::Parser parser(lexResult.tokens);
    return parser.parse();
}

bool hasDiagnostic(const basicc::ParseResult& result, std::string_view text) {
    for (const basicc::Diagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::size_t countText(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string_view::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

void testSampleSnapshotAndAliases() {
    const basicc::ParseResult result = parseSource(
        "function maximum(left: float, right: double) -> float:\n"
        "    variable result = left;\n"
        "    if right > left:\n"
        "        result = right;\n"
        "    return result;\n");
    check(result.ok(), "the representative sample should parse");

    basicc::AstPrinter printer;
    const std::string actual = printer.print(result.program);
    const std::string expected =
        "Program\n"
        "  Function maximum\n"
        "    Parameters\n"
        "      Parameter left: float\n"
        "      Parameter right: float\n"
        "    Returns float\n"
        "    Block\n"
        "      Variable\n"
        "        Binding result: <inferred>\n"
        "          Initializer\n"
        "            Identifier left\n"
        "      If\n"
        "        Condition\n"
        "          Binary >\n"
        "            Identifier right\n"
        "            Identifier left\n"
        "        Then\n"
        "          Block\n"
        "            ExpressionStatement\n"
        "              Assignment\n"
        "                Target\n"
        "                  Identifier result\n"
        "                Value\n"
        "                  Identifier right\n"
        "      Return\n"
        "        Identifier result\n";
    check(actual == expected,
          "the sample AST should be stable and canonicalize aliases");
}

void testVariableFormsAndReturnInference() {
    const basicc::ParseResult result = parseSource(
        "var first = 1;\n"
        "integer second = 2;\n"
        "fn convert(value: integer):\n"
        "    var local: double = 2.5;\n"
        "    return local;\n");
    check(result.ok(), "all variable forms and inferred returns should parse");
    check(result.program.declarations.size() == 3,
          "two globals and one function should be produced");
    if (result.program.declarations.size() == 3) {
        const auto& typedGlobal = static_cast<const basicc::GlobalVariableDeclaration&>(
            *result.program.declarations[1]);
        check(typedGlobal.binding.explicitType.has_value() &&
                  typedGlobal.binding.explicitType->kind == basicc::TypeKind::Int,
              "integer should canonicalize to the int AST type");

        const auto& function = static_cast<const basicc::FunctionDeclaration&>(
            *result.program.declarations[2]);
        check(!function.returnType.has_value(),
              "an omitted function return type should remain inferred");
        check(function.parameters[0].type.kind == basicc::TypeKind::Int,
              "parameter aliases should canonicalize in the AST");
    }
}

void testExpressionPrecedenceAndPostfix() {
    const basicc::ParseResult result = parseSource(
        "fn expressions(items: Data, flag: bool) -> int:\n"
        "    var ordered = a + b * c == d || e && f;\n"
        "    var selected = flag ? items[1].value : call(-1, &items);\n"
        "    items[0].value = selected;\n"
        "    return selected;\n");
    check(result.ok(), "precedence and postfix expressions should parse");

    const auto& function = static_cast<const basicc::FunctionDeclaration&>(
        *result.program.declarations.front());
    const auto& ordered = static_cast<const basicc::VariableDeclarationStatement&>(
        *function.body->statements[0]);
    const auto& logicalOr = static_cast<const basicc::BinaryExpression&>(
        *ordered.binding.initializer);
    check(logicalOr.operation.type == basicc::TokenType::PipePipe,
          "logical OR should be the root of the ordered expression");

    const auto& equality =
        static_cast<const basicc::BinaryExpression&>(*logicalOr.left);
    check(equality.operation.type == basicc::TokenType::EqualEqual,
          "equality should bind more tightly than logical OR");
    const auto& addition =
        static_cast<const basicc::BinaryExpression&>(*equality.left);
    const auto& multiplication =
        static_cast<const basicc::BinaryExpression&>(*addition.right);
    check(addition.operation.type == basicc::TokenType::Plus &&
              multiplication.operation.type == basicc::TokenType::Star,
          "multiplication should bind more tightly than addition");

    basicc::AstPrinter printer;
    const std::string tree = printer.print(result.program);
    check(tree.find("Conditional") != std::string::npos &&
              tree.find("Index") != std::string::npos &&
              tree.find("Member value") != std::string::npos &&
              tree.find("Call") != std::string::npos &&
              tree.find("Unary -") != std::string::npos &&
              tree.find("Unary &") != std::string::npos,
          "ternary, indexing, members, calls, and unary operations should appear");
}

void testControlFlow() {
    const basicc::ParseResult result = parseSource(
        "fn flow(limit: int):\n"
        "    var i = 0;\n"
        "    if i < limit:\n"
        "        while i < limit:\n"
        "            i = i + 1;\n"
        "    else if limit == 0:\n"
        "        return;\n"
        "    else:\n"
        "        return limit;\n"
        "    for (var j = 0; j < limit; j = j + 1):\n"
        "        call(j);\n"
        "    for (;;):\n"
        "        return;\n");
    check(result.ok(), "if, while, and C-style for statements should parse");

    basicc::AstPrinter printer;
    const std::string tree = printer.print(result.program);
    check(countText(tree, "      For\n") == 2,
          "both for statements should appear in the function body");
    check(tree.find("While") != std::string::npos,
          "the while statement should appear in the AST");
    check(tree.find("Else\n          If") != std::string::npos,
          "else-if should be represented as a nested if statement");
    check(countText(tree, "<empty>") >= 4,
          "empty for clauses and empty returns should be explicit in the tree");
}

void testDiagnosticsAndRecovery() {
    {
        const basicc::ParseResult result =
            parseSource("fn bad(value:):\n    return;\n");
        check(hasDiagnostic(result, "expected a type name"),
              "missing parameter types should be diagnosed");
    }
    {
        const basicc::ParseResult result =
            parseSource("fn bad()\n    return;\n");
        check(hasDiagnostic(result, "expected ':'"),
              "missing block colons should be diagnosed");
    }
    {
        const basicc::ParseResult result =
            parseSource("fn bad():\nreturn;\n");
        check(hasDiagnostic(result, "expected an indented block"),
              "missing indentation should be diagnosed");
    }
    {
        const basicc::ParseResult result =
            parseSource("fn bad():\n    int value;\n");
        check(hasDiagnostic(result, "must have an initializer"),
              "missing variable initializers should be diagnosed");
    }
    {
        const basicc::ParseResult result =
            parseSource("fn bad():\n    1 = 2;\n");
        check(hasDiagnostic(result, "not assignable"),
              "invalid assignment targets should be diagnosed");
    }
    {
        const basicc::ParseResult result =
            parseSource("fn bad():\n    for var i = 0; i < 2; i = i + 1:\n"
                        "        return;\n");
        check(hasDiagnostic(result, "expected '('"),
              "missing for-loop parentheses should be diagnosed");
    }
    {
        const basicc::ParseResult result = parseSource(
            "fn bad() -> int:\n"
            "    var first = ;\n"
            "    return 1\n"
            "    var second = ;\n");
        check(hasDiagnostic(result, "expected ';'"),
              "missing statement semicolons should be diagnosed");
        check(result.diagnostics.size() >= 3,
              "the parser should recover and report multiple statement errors");
    }
    {
        const basicc::ParseResult result =
            parseSource("struct Item:\n    var value = 1;\n");
        check(hasDiagnostic(result, "reserved but not supported"),
              "reserved future syntax should have a focused diagnostic");
    }
}

} // namespace

int main() {
    testSampleSnapshotAndAliases();
    testVariableFormsAndReturnInference();
    testExpressionPrecedenceAndPostfix();
    testControlFlow();
    testDiagnosticsAndRecovery();

    if (failures != 0) {
        std::cerr << failures << " parser test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All parser tests passed\n";
    return EXIT_SUCCESS;
}
