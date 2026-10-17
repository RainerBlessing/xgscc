#include <gtest/gtest.h>
#include <string>
#include "ast.h"
#include "errors.h"
#include "parser.h"

namespace {

Program parseSource(const std::string& source) {
    return parse(tokenize(source));
}

// the first statement of main
const Stmt& firstStatement(const Program& program) {
    return *program.functions.at(0).body->statements.at(0);
}

// "x = EXPR;" in main -> the expression in prefix form
std::string expressionOf(const std::string& expression) {
    Program program = parseSource("uint8_t x; void main(void) { x = " + expression + "; }");
    return toString(*firstStatement(program).expr);
}

CompileError errorOf(const std::string& source) {
    try {
        parseSource(source);
    } catch (const CompileError& error) {
        return error;
    }
    ADD_FAILURE() << "no error for: " << source;
    return CompileError(0, "");
}

std::string inMain(const std::string& statements) {
    return "uint8_t a; uint8_t b; void main(void) {\n" + statements + "\n}";
}

}

// expressions

TEST(ParserTest, ShiftBindsLooserThanPlus) {
    EXPECT_EQ("(<< (+ a b) c)", expressionOf("a + b << c"));
}

TEST(ParserTest, BitwiseOperatorsHaveTheirOwnLevels) {
    EXPECT_EQ("(| a (^ b (& c d)))", expressionOf("a | b ^ c & d"));
}

TEST(ParserTest, ComparisonsBindTighterThanEquality) {
    EXPECT_EQ("(== a (< b c))", expressionOf("a == b < c"));
}

TEST(ParserTest, LogicalOrBindsLooserThanAnd) {
    EXPECT_EQ("(|| a (&& b c))", expressionOf("a || b && c"));
}

TEST(ParserTest, BitwiseBindsTighterThanLogical) {
    EXPECT_EQ("(&& (| a b) (& c d))", expressionOf("a | b && c & d"));
}

TEST(ParserTest, ComparisonBindsLooserThanBitwiseAndShift) {
    EXPECT_EQ("(< (<< a 1) (+ b 2))", expressionOf("a << 1 < b + 2"));
}

TEST(ParserTest, BinaryOperatorsAssociateToTheLeft) {
    EXPECT_EQ("(- (- a b) c)", expressionOf("a - b - c"));
}

TEST(ParserTest, ParenthesesOverrideThePrecedence) {
    EXPECT_EQ("(+ a (<< b c))", expressionOf("a + (b << c)"));
    EXPECT_EQ("(<< a (+ b c))", expressionOf("a << (b + c)"));
}

TEST(ParserTest, UnaryOperatorsBindTightest) {
    EXPECT_EQ("(+ (~ a) 1)", expressionOf("~a + 1"));
    EXPECT_EQ("(&& (! a) b)", expressionOf("!a && b"));
    EXPECT_EQ("(~ (! a))", expressionOf("~!a"));
}

TEST(ParserTest, NumbersAndVariablesAreLeaves) {
    EXPECT_EQ("200", expressionOf("200"));
    EXPECT_EQ("a", expressionOf("a"));
    EXPECT_EQ("(- 5)", expressionOf("-5"));
}

TEST(ParserTest, MultiplyDivideModuloAreNotSupportedYet) {
    for (const char* op : { "*", "/", "%" }) {
        CompileError error = errorOf(inMain(std::string("a = b ") + op + " 2;"));
        EXPECT_EQ(2, error.line);
        EXPECT_NE(std::string::npos, error.message.find(op)) << error.message;
    }
}

// statements

TEST(ParserTest, GlobalVariableWithAndWithoutInitializer) {
    Program program = parseSource("uint8_t a = 5; uint8_t b; void main(void) { }");
    ASSERT_EQ(2u, program.globals.size());
    EXPECT_EQ("a", program.globals[0]->name);
    EXPECT_EQ("5", toString(*program.globals[0]->expr));
    EXPECT_EQ("b", program.globals[1]->name);
    EXPECT_FALSE(program.globals[1]->expr);
}

TEST(ParserTest, UnsignedCharIsAnotherNameOfUint8) {
    Program program = parseSource("unsigned char a; void main(void) { }");
    ASSERT_EQ(1u, program.globals.size());
    EXPECT_EQ("a", program.globals[0]->name);
}

TEST(ParserTest, MainMayHaveEmptyParentheses) {
    Program program = parseSource("void main() { }");
    ASSERT_EQ(1u, program.functions.size());
    EXPECT_EQ("main", program.functions[0].name);
}

TEST(ParserTest, AssignmentKeepsTheOperator) {
    Program program = parseSource(inMain("a = 1; a += 2; a <<= 3;"));
    const Function& main = program.functions[0];
    ASSERT_EQ(3u, main.body->statements.size());
    EXPECT_EQ(Stmt::ASSIGNMENT, main.body->statements[1]->kind);
    EXPECT_EQ("a", main.body->statements[1]->name);
    EXPECT_EQ("+=", main.body->statements[1]->op);
    EXPECT_EQ("<<=", main.body->statements[2]->op);
}

TEST(ParserTest, IncrementAndDecrementAsStatements) {
    Program program = parseSource(inMain("a++; ++a; a--; --b;"));
    const std::vector<StmtPtr>& s = program.functions[0].body->statements;
    ASSERT_EQ(4u, s.size());
    for (const StmtPtr& statement : s) {
        EXPECT_EQ(Stmt::STEP, statement->kind);
    }
    EXPECT_EQ("++", s[0]->op);
    EXPECT_EQ("++", s[1]->op);
    EXPECT_EQ("--", s[2]->op);
    EXPECT_EQ("b", s[3]->name);
}

TEST(ParserTest, LocalDeclarationInABlock) {
    Program program = parseSource("void main(void) { uint8_t i = 3; { uint8_t j; } }");
    const std::vector<StmtPtr>& s = program.functions[0].body->statements;
    ASSERT_EQ(2u, s.size());
    EXPECT_EQ(Stmt::DECLARATION, s[0]->kind);
    EXPECT_EQ(Stmt::BLOCK, s[1]->kind);
}

TEST(ParserTest, IfWithAndWithoutElse) {
    Program program = parseSource(inMain("if (a) b = 1; if (a) b = 2; else b = 3;"));
    const std::vector<StmtPtr>& s = program.functions[0].body->statements;
    EXPECT_EQ(Stmt::IF, s[0]->kind);
    EXPECT_FALSE(s[0]->elseBody);
    EXPECT_TRUE(s[1]->elseBody);
}

TEST(ParserTest, ElseBelongsToTheNearestIf) {
    Program program = parseSource(inMain("if (a) if (b) a = 1; else a = 2;"));
    const Stmt& outer = firstStatement(program);
    EXPECT_FALSE(outer.elseBody);
    ASSERT_TRUE(outer.body);
    EXPECT_TRUE(outer.body->elseBody);
}

TEST(ParserTest, WhileHasConditionAndBody) {
    Program program = parseSource(inMain("while (a < 10) { a++; }"));
    const Stmt& loop = firstStatement(program);
    EXPECT_EQ(Stmt::WHILE, loop.kind);
    EXPECT_EQ("(< a 10)", toString(*loop.expr));
    EXPECT_EQ(Stmt::BLOCK, loop.body->kind);
}

TEST(ParserTest, ForHasInitConditionStepAndBody) {
    Program program = parseSource(inMain("for (a = 0; a < 10; a++) b++;"));
    const Stmt& loop = firstStatement(program);
    EXPECT_EQ(Stmt::FOR, loop.kind);
    EXPECT_EQ(Stmt::ASSIGNMENT, loop.init->kind);
    EXPECT_EQ("(< a 10)", toString(*loop.expr));
    EXPECT_EQ(Stmt::STEP, loop.step->kind);
    EXPECT_EQ(Stmt::STEP, loop.body->kind);
}

TEST(ParserTest, ForPartsMayBeEmpty) {
    Program program = parseSource(inMain("for (;;) a++;"));
    const Stmt& loop = firstStatement(program);
    EXPECT_FALSE(loop.init);
    EXPECT_FALSE(loop.expr);
    EXPECT_FALSE(loop.step);
}

TEST(ParserTest, ForMayDeclareItsVariable) {
    Program program = parseSource(inMain("for (uint8_t i = 0; i < 3; i++) a++;"));
    EXPECT_EQ(Stmt::DECLARATION, firstStatement(program).init->kind);
}

TEST(ParserTest, EmptyStatement) {
    Program program = parseSource(inMain(";"));
    EXPECT_EQ(Stmt::EMPTY, firstStatement(program).kind);
}

// errors

TEST(ParserTest, MissingSemicolonNamesTheLine) {
    CompileError error = errorOf(inMain("a = 1\nb = 2;"));
    EXPECT_EQ(3, error.line);
    EXPECT_NE(std::string::npos, error.message.find("';'")) << error.message;
}

TEST(ParserTest, MissingOperandIsAnError) {
    EXPECT_EQ(2, errorOf(inMain("a = ;")).line);
    EXPECT_EQ(2, errorOf(inMain("a = 1 + ;")).line);
}

TEST(ParserTest, UnclosedBlockIsAnError) {
    EXPECT_EQ(1, errorOf("void main(void) { ").line);
}

TEST(ParserTest, UnclosedParenthesisIsAnError) {
    EXPECT_EQ(2, errorOf(inMain("a = (1 + 2;")).line);
}

TEST(ParserTest, AssignmentIsNoExpression) {
    CompileError error = errorOf(inMain("a = b = 1;"));
    EXPECT_NE(std::string::npos, error.message.find("assignment")) << error.message;
}

TEST(ParserTest, IncrementIsNoExpression) {
    CompileError error = errorOf(inMain("a = b++;"));
    EXPECT_NE(std::string::npos, error.message.find("++")) << error.message;
}

TEST(ParserTest, OtherTypesAreNotSupportedYet) {
    for (const char* type : { "int", "char", "short", "long", "signed", "float" }) {
        CompileError error = errorOf(std::string(type) + " x; void main(void) { }");
        EXPECT_EQ(1, error.line);
        EXPECT_NE(std::string::npos, error.message.find(type)) << error.message;
    }
}

TEST(ParserTest, StatementsOfLaterStagesAreNotSupportedYet) {
    for (const char* keyword : { "switch", "goto", "case", "default" }) {
        CompileError error = errorOf(inMain(std::string(keyword) + " ;"));
        EXPECT_EQ(2, error.line);
        EXPECT_NE(std::string::npos, error.message.find(keyword)) << error.message;
    }
}

TEST(ParserTest, GarbageAtTheTopLevelIsAnError) {
    EXPECT_EQ(1, errorOf("5;").line);
    EXPECT_EQ(1, errorOf("}").line);
}

TEST(ParserTest, CompoundAssignmentOfLaterStagesIsNotSupportedYet) {
    CompileError error = errorOf(inMain("a *= 2;"));
    EXPECT_NE(std::string::npos, error.message.find("*=")) << error.message;
}

// functions

TEST(ParserTest, FunctionWithParametersAndAReturnValue) {
    Program program = parseSource("uint8_t add(uint8_t a, uint8_t b) { return a + b; } void main(void) { }");
    ASSERT_EQ(2u, program.functions.size());
    const Function& add = program.functions[0];
    EXPECT_EQ("add", add.name);
    EXPECT_TRUE(add.returnsValue);
    ASSERT_EQ(2u, add.parameters.size());
    EXPECT_EQ("a", add.parameters[0].name);
    EXPECT_EQ("b", add.parameters[1].name);
    EXPECT_FALSE(program.functions[1].returnsValue);
}

TEST(ParserTest, VoidParameterListMeansNoParameters) {
    Program program = parseSource("void f(void) { } void g() { }");
    EXPECT_TRUE(program.functions[0].parameters.empty());
    EXPECT_TRUE(program.functions[1].parameters.empty());
}

TEST(ParserTest, ConstParameterAndUnsignedChar) {
    Program program = parseSource("void f(const uint8_t a, unsigned char b) { }");
    EXPECT_EQ(2u, program.functions[0].parameters.size());
}

TEST(ParserTest, PrototypeHasNoBody) {
    Program program = parseSource("uint8_t f(uint8_t x); void main(void) { }");
    ASSERT_EQ(1u, program.prototypes.size());
    EXPECT_EQ("f", program.prototypes[0].name);
    EXPECT_TRUE(program.prototypes[0].returnsValue);
    EXPECT_EQ(1u, program.prototypes[0].parameters.size());
    EXPECT_FALSE(program.prototypes[0].body);
    EXPECT_EQ(1u, program.functions.size());
}

TEST(ParserTest, CallInAnExpression) {
    EXPECT_EQ("(+ (call f 1 (+ a 2)) 3)", expressionOf("f(1, a + 2) + 3"));
    EXPECT_EQ("(call f)", expressionOf("f()"));
    EXPECT_EQ("(call f (call g a))", expressionOf("f(g(a))"));
}

TEST(ParserTest, CallAsAStatement) {
    Program program = parseSource(inMain("f(1); g();"));
    const std::vector<StmtPtr>& s = program.functions[0].body->statements;
    ASSERT_EQ(2u, s.size());
    EXPECT_EQ(Stmt::CALL, s[0]->kind);
    EXPECT_EQ("(call f 1)", toString(*s[0]->expr));
    EXPECT_EQ(Stmt::CALL, s[1]->kind);
}

TEST(ParserTest, CallInForInitAndStep) {
    Program program = parseSource(inMain("for (f(); a < 3; g(a)) ;"));
    const Stmt& loop = firstStatement(program);
    EXPECT_EQ(Stmt::CALL, loop.init->kind);
    EXPECT_EQ(Stmt::CALL, loop.step->kind);
}

TEST(ParserTest, CallNeedsAClosingParenthesis) {
    CompileError error = errorOf(inMain("a = f(1, 2;"));
    EXPECT_EQ(2, error.line);
    EXPECT_NE(std::string::npos, error.message.find("')'")) << error.message;
}

TEST(ParserTest, ArgumentsAreExpressionsNotAssignments) {
    EXPECT_NE(std::string::npos, errorOf(inMain("f(a = 1);")).message.find("assignment"));
}

TEST(ParserTest, VariableMayNotBeVoid) {
    CompileError error = errorOf("void x; void main(void) { }");
    EXPECT_EQ(1, error.line);
    EXPECT_NE(std::string::npos, error.message.find("void")) << error.message;
}

TEST(ParserTest, ParameterOfAnotherTypeIsNotSupportedYet) {
    EXPECT_NE(std::string::npos, errorOf("void f(int x) { }").message.find("int"));
}

TEST(ParserTest, ParameterNeedsAName) {
    EXPECT_EQ(1, errorOf("void f(uint8_t) { }").line);
}

TEST(ParserTest, ReturnWithAndWithoutValue) {
    Program program = parseSource("uint8_t f(void) { return 1 + 2; } void g(void) { return; }");
    const Stmt& withValue = *program.functions[0].body->statements.at(0);
    EXPECT_EQ(Stmt::RETURN, withValue.kind);
    EXPECT_EQ("(+ 1 2)", toString(*withValue.expr));
    const Stmt& without = *program.functions[1].body->statements.at(0);
    EXPECT_EQ(Stmt::RETURN, without.kind);
    EXPECT_FALSE(without.expr);
}

TEST(ParserTest, BreakAndContinue) {
    Program program = parseSource(inMain("while (a) { break; continue; }"));
    const Stmt& loop = firstStatement(program);
    EXPECT_EQ(Stmt::BREAK, loop.body->statements.at(0)->kind);
    EXPECT_EQ(Stmt::CONTINUE, loop.body->statements.at(1)->kind);
}

TEST(ParserTest, DoWhile) {
    Program program = parseSource(inMain("do a++; while (a < 5);"));
    const Stmt& loop = firstStatement(program);
    EXPECT_EQ(Stmt::DO_WHILE, loop.kind);
    EXPECT_EQ("(< a 5)", toString(*loop.expr));
    EXPECT_EQ(Stmt::STEP, loop.body->kind);
}

TEST(ParserTest, DoWhileNeedsItsSemicolon) {
    EXPECT_NE(std::string::npos, errorOf(inMain("do a++; while (a < 5)\nb = 1;")).message.find("';'"));
}

TEST(ParserTest, DoNeedsWhile) {
    EXPECT_NE(std::string::npos, errorOf(inMain("do a++; b = 1;")).message.find("while"));
}

TEST(ParserTest, StaticAndConstQualifiers) {
    Program program = parseSource("static uint8_t a; const uint8_t b = 1; static const uint8_t c = 2; const static uint8_t d = 3;"
                                  "void main(void) { static uint8_t e; const uint8_t f = 4; }");
    ASSERT_EQ(4u, program.globals.size());
    EXPECT_TRUE(program.globals[0]->isStatic);
    EXPECT_FALSE(program.globals[0]->isConst);
    EXPECT_TRUE(program.globals[1]->isConst);
    EXPECT_TRUE(program.globals[2]->isStatic && program.globals[2]->isConst);
    EXPECT_TRUE(program.globals[3]->isStatic && program.globals[3]->isConst);
    const std::vector<StmtPtr>& s = program.functions[0].body->statements;
    EXPECT_TRUE(s[0]->isStatic);
    EXPECT_TRUE(s[1]->isConst);
}

TEST(ParserTest, QualifierTwiceIsAnError) {
    EXPECT_NE(std::string::npos, errorOf("const const uint8_t a; void main(void) { }").message.find("const"));
}

TEST(ParserTest, StaticFunctionIsAccepted) {
    Program program = parseSource("static void f(void) { } void main(void) { }");
    EXPECT_EQ(2u, program.functions.size());
}
