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

TEST(ParserTest, MultiplyDivideModuloBindTighterThanPlus) {
    EXPECT_EQ("(+ a (* b c))", expressionOf("a + b * c"));
    EXPECT_EQ("(+ (/ a b) c)", expressionOf("a / b + c"));
    EXPECT_EQ("(- a (% b c))", expressionOf("a - b % c"));
    EXPECT_EQ("(* (+ a b) c)", expressionOf("(a + b) * c"));
}

TEST(ParserTest, MultiplyAssociatesToTheLeftAndBindsTighterThanShift) {
    EXPECT_EQ("(/ (* a b) c)", expressionOf("a * b / c"));
    EXPECT_EQ("(<< a (* b 2))", expressionOf("a << b * 2"));
    EXPECT_EQ("(< (* a b) c)", expressionOf("a * b < c"));
}

TEST(ParserTest, StarInTheMiddleIsMultiplyAndAtTheStartIsDereference) {
    EXPECT_EQ("(* a (* b))", expressionOf("a * *b"));
    EXPECT_EQ("(* (* a) b)", expressionOf("*a * b"));
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
    CompileError error = errorOf(inMain("goto ;"));
    EXPECT_EQ(2, error.line);
    EXPECT_NE(std::string::npos, error.message.find("goto")) << error.message;
}

TEST(ParserTest, GarbageAtTheTopLevelIsAnError) {
    EXPECT_EQ(1, errorOf("5;").line);
    EXPECT_EQ(1, errorOf("}").line);
}

TEST(ParserTest, MultiplyDivideModuloAssignments) {
    Program program = parseSource(inMain("a *= 2; a /= 3; a %= 4;"));
    const std::vector<StmtPtr>& s = program.functions[0].body->statements;
    EXPECT_EQ("*=", s[0]->op);
    EXPECT_EQ("/=", s[1]->op);
    EXPECT_EQ("%=", s[2]->op);
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

// types

TEST(ParserTest, DeclaresVariablesOfAllTypes) {
    Program program = parseSource("uint8_t a; int8_t b; uint16_t c; int16_t d; unsigned char e; signed char f;"
                                  "void main(void) { }");
    ASSERT_EQ(6u, program.globals.size());
    const struct { int size; bool isSigned; } expected[] = { {1, false}, {1, true}, {2, false}, {2, true}, {1, false}, {1, true} };
    for (size_t i = 0; i < 6; i++) {
        EXPECT_EQ(expected[i].size, program.globals[i]->type.size) << program.globals[i]->name;
        EXPECT_EQ(expected[i].isSigned, program.globals[i]->type.isSigned) << program.globals[i]->name;
    }
}

TEST(ParserTest, TypeNames) {
    EXPECT_EQ("uint8_t", typeName(Type(1, false)));
    EXPECT_EQ("int8_t", typeName(Type(1, true)));
    EXPECT_EQ("uint16_t", typeName(Type(2, false)));
    EXPECT_EQ("int16_t", typeName(Type(2, true)));
}

TEST(ParserTest, LocalsAndFunctionsHaveTypes) {
    Program program = parseSource("int16_t f(uint16_t a, int8_t b) { int16_t x = 1; return x; } void main(void) { }");
    const Function& f = program.functions[0];
    EXPECT_TRUE(f.returnsValue);
    EXPECT_EQ(2, f.returnType.size);
    EXPECT_TRUE(f.returnType.isSigned);
    EXPECT_EQ(2, f.parameters[0].type.size);
    EXPECT_FALSE(f.parameters[0].type.isSigned);
    EXPECT_EQ(1, f.parameters[1].type.size);
    EXPECT_TRUE(f.parameters[1].type.isSigned);
    EXPECT_EQ(2, f.body->statements[0]->type.size);
}

TEST(ParserTest, Cast) {
    EXPECT_EQ("(cast uint8_t a)", expressionOf("(uint8_t)a"));
    EXPECT_EQ("(+ (cast int16_t a) 1)", expressionOf("(int16_t)a + 1"));
    EXPECT_EQ("(cast uint16_t (+ a b))", expressionOf("(uint16_t)(a + b)"));
    EXPECT_EQ("(cast uint8_t (cast int8_t a))", expressionOf("(uint8_t)(int8_t)a"));
    EXPECT_EQ("(cast uint8_t (- a))", expressionOf("(uint8_t)-a"));
}

TEST(ParserTest, ParenthesesAroundANameAreNoCast) {
    EXPECT_EQ("(+ a b)", expressionOf("(a) + b"));
}

TEST(ParserTest, CastToAnotherTypeIsNotSupportedYet) {
    EXPECT_NE(std::string::npos, errorOf(inMain("a = (int)b;")).message.find("int"));
}

TEST(ParserTest, CastToVoidIsAnError) {
    EXPECT_NE(std::string::npos, errorOf(inMain("a = (void)b;")).message.find("void"));
}

TEST(ParserTest, CastNeedsItsClosingParenthesis) {
    EXPECT_NE(std::string::npos, errorOf(inMain("a = (uint8_t b;")).message.find("')'"));
}

TEST(ParserTest, SignedAloneIsNotAType) {
    EXPECT_NE(std::string::npos, errorOf("signed x; void main(void) { }").message.find("signed"));
}

// pointers and arrays

TEST(ParserTest, PointerDeclarations) {
    Program program = parseSource("uint8_t *p; int16_t *q; uint8_t* r; void main(void) { }");
    ASSERT_EQ(3u, program.globals.size());
    EXPECT_TRUE(program.globals[0]->type.isPointer);
    EXPECT_EQ(1, program.globals[0]->type.pointeeSize);
    EXPECT_FALSE(program.globals[0]->type.pointeeSigned);
    EXPECT_TRUE(program.globals[1]->type.isPointer);
    EXPECT_EQ(2, program.globals[1]->type.pointeeSize);
    EXPECT_TRUE(program.globals[1]->type.pointeeSigned);
    EXPECT_TRUE(program.globals[2]->type.isPointer);
    EXPECT_EQ(1, program.globals[2]->type.size);        // the address is one byte
}

TEST(ParserTest, PointerTypeNames) {
    EXPECT_EQ("uint8_t *", typeName(pointerTo(Type(1, false))));
    EXPECT_EQ("int16_t *", typeName(pointerTo(Type(2, true))));
}

TEST(ParserTest, ConstPointeeDoesNotMakeThePointerConst) {
    Program program = parseSource("const uint8_t *p; void main(void) { }");
    EXPECT_TRUE(program.globals[0]->type.isPointer);
    EXPECT_FALSE(program.globals[0]->isConst);
}

TEST(ParserTest, PointerToPointerIsNotSupportedYet) {
    EXPECT_NE(std::string::npos, errorOf("uint8_t **p; void main(void) { }").message.find("pointer"));
}

TEST(ParserTest, ArrayDeclarations) {
    Program program = parseSource("uint8_t a[16]; int16_t b[4]; uint8_t c[3] = { 1, 2, 3 }; uint8_t d[] = { 5, 6 }; void main(void) { }");
    ASSERT_EQ(4u, program.globals.size());
    EXPECT_EQ(16, program.globals[0]->arraySize);
    EXPECT_EQ(4, program.globals[1]->arraySize);
    EXPECT_EQ(2, program.globals[1]->type.size);
    EXPECT_EQ(3, program.globals[2]->arraySize);
    ASSERT_EQ(3u, program.globals[2]->elements.size());
    EXPECT_EQ("2", toString(*program.globals[2]->elements[1]));
    EXPECT_EQ(2, program.globals[3]->arraySize);        // from the initializer
}

TEST(ParserTest, ArrayOfPointers) {
    Program program = parseSource("uint8_t *table[4]; void main(void) { }");
    EXPECT_EQ(4, program.globals[0]->arraySize);
    EXPECT_TRUE(program.globals[0]->type.isPointer);
}

TEST(ParserTest, LocalArray) {
    Program program = parseSource("void main(void) { uint8_t buffer[8]; }");
    EXPECT_EQ(8, program.functions[0].body->statements[0]->arraySize);
}

TEST(ParserTest, ArrayNeedsASize) {
    EXPECT_NE(std::string::npos, errorOf("uint8_t a[]; void main(void) { }").message.find("size"));
    EXPECT_NE(std::string::npos, errorOf("uint8_t a[0]; void main(void) { }").message.find("size"));
}

TEST(ParserTest, ArraySizeIsANumber) {
    EXPECT_NE(std::string::npos, errorOf("uint8_t n; uint8_t a[n]; void main(void) { }").message.find("number"));
}

TEST(ParserTest, TooManyInitializers) {
    CompileError error = errorOf("uint8_t a[2] = { 1, 2, 3 };\nvoid main(void) { }");
    EXPECT_EQ(1, error.line);
    EXPECT_NE(std::string::npos, error.message.find("initializer")) << error.message;
}

TEST(ParserTest, InitializerListOnlyForArrays) {
    EXPECT_NE(std::string::npos, errorOf("uint8_t a = { 1 }; void main(void) { }").message.find("array"));
}

TEST(ParserTest, ArrayNeedsAnInitializerList) {
    EXPECT_NE(std::string::npos, errorOf("uint8_t a[2] = 5; void main(void) { }").message.find("{"));
}

TEST(ParserTest, PointerParameterAndResult) {
    Program program = parseSource("uint8_t *f(uint8_t *a, int16_t *b) { return a; } void main(void) { }");
    const Function& f = program.functions[0];
    EXPECT_TRUE(f.returnsValue);
    EXPECT_TRUE(f.returnType.isPointer);
    EXPECT_TRUE(f.parameters[0].type.isPointer);
    EXPECT_EQ(2, f.parameters[1].type.pointeeSize);
}

TEST(ParserTest, DereferenceAddressAndIndex) {
    EXPECT_EQ("(* a)", expressionOf("*a"));
    EXPECT_EQ("(& a)", expressionOf("&a"));
    EXPECT_EQ("(index a b)", expressionOf("a[b]"));
    EXPECT_EQ("(index a (+ b 1))", expressionOf("a[b + 1]"));
    EXPECT_EQ("(& (index a 2))", expressionOf("&a[2]"));
    EXPECT_EQ("(+ (* a) 1)", expressionOf("*a + 1"));
    EXPECT_EQ("(* (+ a 1))", expressionOf("*(a + 1)"));
    EXPECT_EQ("(* (index a 1))", expressionOf("*a[1]"));
    EXPECT_EQ("(index (index a 1) 2)", expressionOf("a[1][2]"));
    EXPECT_EQ("(* (* a))", expressionOf("**a"));
}

TEST(ParserTest, IndexNeedsItsClosingBracket) {
    EXPECT_NE(std::string::npos, errorOf(inMain("a = b[1;")).message.find("']'"));
}

TEST(ParserTest, CastToAPointer) {
    EXPECT_EQ("(cast uint8_t * a)", expressionOf("(uint8_t *)a"));
    EXPECT_EQ("(cast int16_t * (+ a 1))", expressionOf("(int16_t*)(a + 1)"));
}

TEST(ParserTest, AssignmentToAPointerTarget) {
    Program program = parseSource(inMain("*a = 3; a[b] += 2; a[0]++; (*a)--;"));
    const std::vector<StmtPtr>& s = program.functions[0].body->statements;
    ASSERT_EQ(4u, s.size());
    EXPECT_EQ(Stmt::ASSIGNMENT, s[0]->kind);
    ASSERT_TRUE(s[0]->target);
    EXPECT_EQ("(* a)", toString(*s[0]->target));
    EXPECT_EQ("=", s[0]->op);
    EXPECT_EQ(Stmt::ASSIGNMENT, s[1]->kind);
    EXPECT_EQ("(index a b)", toString(*s[1]->target));
    EXPECT_EQ("+=", s[1]->op);
    EXPECT_EQ(Stmt::STEP, s[2]->kind);
    EXPECT_EQ("(index a 0)", toString(*s[2]->target));
    EXPECT_EQ(Stmt::STEP, s[3]->kind);
    EXPECT_EQ("(* a)", toString(*s[3]->target));
}

TEST(ParserTest, AssignmentToAVariableHasNoTarget) {
    Program program = parseSource(inMain("a = 3; a++;"));
    EXPECT_FALSE(program.functions[0].body->statements[0]->target);
    EXPECT_EQ("a", program.functions[0].body->statements[0]->name);
    EXPECT_FALSE(program.functions[0].body->statements[1]->target);
}

TEST(ParserTest, StarThenIncrementIsAmbiguous) {
    CompileError error = errorOf(inMain("*a++;"));
    EXPECT_EQ(2, error.line);
    EXPECT_NE(std::string::npos, error.message.find("(*")) << error.message;
}

TEST(ParserTest, CannotAssignToAnExpression) {
    EXPECT_NE(std::string::npos, errorOf(inMain("a + 1 = 2;")).message.find("assign"));
    EXPECT_NE(std::string::npos, errorOf(inMain("5 = a;")).message.find("statement"));
}

TEST(ParserTest, IndexedMultiplyAssignment) {
    Program program = parseSource(inMain("a[1] *= 2;"));
    EXPECT_EQ("*=", program.functions[0].body->statements[0]->op);
    EXPECT_EQ("(index a 1)", toString(*program.functions[0].body->statements[0]->target));
}

TEST(ParserTest, PointerDeclarationInAFor) {
    Program program = parseSource(inMain("for (uint8_t *p = a; p < b; p++) ;"));
    EXPECT_TRUE(firstStatement(program).init->type.isPointer);
}

// tables and strings

TEST(ParserTest, ConstArrayIsATable) {
    Program program = parseSource("const uint8_t t[] = { 1, 2, 3 }; const int16_t u[2] = { -1, 5 }; void main(void) { }");
    ASSERT_EQ(2u, program.globals.size());
    EXPECT_TRUE(program.globals[0]->isConst);
    EXPECT_EQ(3, program.globals[0]->arraySize);
    EXPECT_TRUE(program.globals[1]->isConst);
    EXPECT_EQ(2, program.globals[1]->type.size);
}

TEST(ParserTest, ConstArrayInAFunction) {
    Program program = parseSource("void main(void) { static const uint8_t t[2] = { 1, 2 }; const uint8_t u[1] = { 7 }; }");
    EXPECT_TRUE(program.functions[0].body->statements[0]->isConst);
    EXPECT_TRUE(program.functions[0].body->statements[1]->isConst);
}

TEST(ParserTest, StringInitializesAnArray) {
    Program program = parseSource("uint8_t s[] = \"ab\"; void main(void) { }");
    EXPECT_EQ(3, program.globals[0]->arraySize);        // with the 0 at the end
    ASSERT_EQ(3u, program.globals[0]->elements.size());
    EXPECT_EQ("97", toString(*program.globals[0]->elements[0]));
    EXPECT_EQ("98", toString(*program.globals[0]->elements[1]));
    EXPECT_EQ("0", toString(*program.globals[0]->elements[2]));
}

TEST(ParserTest, StringInAConstTable) {
    Program program = parseSource("const uint8_t s[] = \"Hi\\n\"; void main(void) { }");
    EXPECT_EQ(4, program.globals[0]->arraySize);
    EXPECT_EQ("10", toString(*program.globals[0]->elements[2]));
}

TEST(ParserTest, StringThatFillsTheArrayHasNoTerminator) {
    Program program = parseSource("uint8_t s[2] = \"ab\"; uint8_t t[4] = \"ab\"; void main(void) { }");
    EXPECT_EQ(2u, program.globals[0]->elements.size());
    EXPECT_EQ(3u, program.globals[1]->elements.size());     // the rest is filled with zeros later
}

TEST(ParserTest, StringTooLongForTheArray) {
    CompileError error = errorOf("uint8_t s[1] = \"ab\";\nvoid main(void) { }");
    EXPECT_EQ(1, error.line);
    EXPECT_NE(std::string::npos, error.message.find("initializers")) << error.message;
}

TEST(ParserTest, StringOnlyForArraysOfBytes) {
    EXPECT_NE(std::string::npos, errorOf("uint16_t s[] = \"ab\"; void main(void) { }").message.find("8 bit"));
    EXPECT_NE(std::string::npos, errorOf("uint8_t s = \"ab\"; void main(void) { }").message.find("string"));
}

TEST(ParserTest, StringInAnExpressionIsAnError) {
    EXPECT_NE(std::string::npos, errorOf(inMain("a = \"x\";")).message.find("string"));
}

// switch

TEST(ParserTest, SwitchWithCasesAndDefault) {
    Program program = parseSource(inMain("switch (a) { case 1: b = 1; break; case 2: b = 2; default: b = 3; }"));
    const Stmt& sw = firstStatement(program);
    EXPECT_EQ(Stmt::SWITCH, sw.kind);
    EXPECT_EQ("a", toString(*sw.expr));
    ASSERT_TRUE(sw.body);
    const std::vector<StmtPtr>& s = sw.body->statements;
    ASSERT_EQ(7u, s.size());
    EXPECT_EQ(Stmt::CASE, s[0]->kind);
    EXPECT_EQ("1", toString(*s[0]->expr));
    EXPECT_EQ(Stmt::ASSIGNMENT, s[1]->kind);
    EXPECT_EQ(Stmt::BREAK, s[2]->kind);
    EXPECT_EQ(Stmt::CASE, s[3]->kind);
    EXPECT_EQ(Stmt::ASSIGNMENT, s[4]->kind);
    EXPECT_EQ(Stmt::DEFAULT, s[5]->kind);
    EXPECT_EQ(Stmt::ASSIGNMENT, s[6]->kind);
}

TEST(ParserTest, StackedCasesAndConstantExpressions) {
    Program program = parseSource(inMain("switch (a) { case 1: case 2 + 3: case 'x': break; }"));
    const std::vector<StmtPtr>& s = firstStatement(program).body->statements;
    ASSERT_EQ(4u, s.size());
    EXPECT_EQ("(+ 2 3)", toString(*s[1]->expr));
    EXPECT_EQ("120", toString(*s[2]->expr));
}

TEST(ParserTest, SwitchBodyMayDeclareAndNest) {
    Program program = parseSource(inMain("switch (a) { case 1: { uint8_t t = 2; switch (t) { default: break; } } }"));
    EXPECT_EQ(Stmt::SWITCH, firstStatement(program).kind);
}

TEST(ParserTest, EmptySwitch) {
    Program program = parseSource(inMain("switch (a) { }"));
    EXPECT_TRUE(firstStatement(program).body->statements.empty());
}

TEST(ParserTest, CaseNeedsItsColon) {
    CompileError error = errorOf(inMain("switch (a) { case 1 b = 1; }"));
    EXPECT_NE(std::string::npos, error.message.find("':'")) << error.message;
}

TEST(ParserTest, DefaultNeedsItsColon) {
    EXPECT_NE(std::string::npos, errorOf(inMain("switch (a) { default b = 1; }")).message.find("':'"));
}

TEST(ParserTest, SwitchNeedsABlock) {
    EXPECT_NE(std::string::npos, errorOf(inMain("switch (a) b = 1;")).message.find("'{'"));
}

TEST(ParserTest, CaseOutsideOfASwitch) {
    CompileError error = errorOf(inMain("case 1: b = 1;"));
    EXPECT_EQ(2, error.line);
    EXPECT_NE(std::string::npos, error.message.find("switch")) << error.message;
    EXPECT_NE(std::string::npos, errorOf(inMain("default: b = 1;")).message.find("switch"));
}

// structs

TEST(ParserTest, StructDefinitionHasMembersOffsetsAndSize) {
    Program program = parseSource("struct Sprite { uint8_t x; int8_t dx; uint16_t score; uint8_t name[4]; uint8_t *link; };"
                                  "void main(void) { }");
    ASSERT_EQ(1u, program.structs.size());
    const StructDefinition& sprite = program.structs[0];
    EXPECT_EQ("Sprite", sprite.name);
    ASSERT_EQ(5u, sprite.members.size());
    EXPECT_EQ("x", sprite.members[0].name);
    EXPECT_EQ(0, sprite.members[0].offset);
    EXPECT_EQ(1, sprite.members[1].offset);
    EXPECT_TRUE(sprite.members[1].type.isSigned);
    EXPECT_EQ(2, sprite.members[2].offset);
    EXPECT_EQ(2, sprite.members[2].type.size);
    EXPECT_EQ(4, sprite.members[3].offset);
    EXPECT_EQ(4, sprite.members[3].count);
    EXPECT_EQ(8, sprite.members[4].offset);
    EXPECT_TRUE(sprite.members[4].type.isPointer);
    EXPECT_EQ(9, sprite.size);
}

TEST(ParserTest, VariablesOfAStructType) {
    Program program = parseSource("struct P { uint8_t x; uint8_t y; };"
                                  "struct P a; struct P b[4]; struct P *c; void main(void) { }");
    ASSERT_EQ(3u, program.globals.size());
    EXPECT_EQ(1, program.globals[0]->type.structId);
    EXPECT_EQ(0, program.globals[0]->arraySize);
    EXPECT_EQ(1, program.globals[1]->type.structId);
    EXPECT_EQ(4, program.globals[1]->arraySize);
    EXPECT_TRUE(program.globals[2]->type.isPointer);
    EXPECT_EQ(1, program.globals[2]->type.pointeeStruct);
}

TEST(ParserTest, StructPointerInsideItself) {
    Program program = parseSource("struct Node { uint8_t value; struct Node *next; }; void main(void) { }");
    ASSERT_EQ(1u, program.structs.size());
    EXPECT_TRUE(program.structs[0].members[1].type.isPointer);
    EXPECT_EQ(1, program.structs[0].members[1].type.pointeeStruct);
    EXPECT_EQ(2, program.structs[0].size);
}

TEST(ParserTest, TwoStructs) {
    Program program = parseSource("struct A { uint8_t a; }; struct B { uint16_t b; struct A *a; }; struct B v; void main(void) { }");
    ASSERT_EQ(2u, program.structs.size());
    EXPECT_EQ(2, program.globals[0]->type.structId);
    EXPECT_EQ(1, program.structs[1].members[1].type.pointeeStruct);
}

TEST(ParserTest, TypedefOfAStruct) {
    Program program = parseSource("typedef struct { uint8_t x; uint8_t y; } Point; typedef struct Q { uint8_t z; } Quad;"
                                  "Point p; Quad q; Point *pp; void main(void) { }");
    ASSERT_EQ(2u, program.structs.size());
    EXPECT_EQ("Point", program.structs[0].name);
    EXPECT_EQ(1, program.globals[0]->type.structId);
    EXPECT_EQ(2, program.globals[1]->type.structId);
    EXPECT_EQ(1, program.globals[2]->type.pointeeStruct);
}

TEST(ParserTest, TypedefOfAnIntegerType) {
    Program program = parseSource("typedef uint8_t byte; typedef uint16_t word; byte a; word b; byte c[3]; byte *d; void main(void) { byte local = 1; }");
    EXPECT_EQ(1, program.globals[0]->type.size);
    EXPECT_EQ(2, program.globals[1]->type.size);
    EXPECT_EQ(3, program.globals[2]->arraySize);
    EXPECT_TRUE(program.globals[3]->type.isPointer);
    EXPECT_EQ(Stmt::DECLARATION, program.functions[0].body->statements[0]->kind);
}

TEST(ParserTest, TypedefNameInAFunctionAndAsParameter) {
    Program program = parseSource("typedef struct { uint8_t x; } P; uint8_t get(P *p) { return p->x; } void main(void) { P v; }");
    EXPECT_TRUE(program.functions[0].parameters[0].type.isPointer);
    EXPECT_EQ(1, program.functions[0].parameters[0].type.pointeeStruct);
}

TEST(ParserTest, LocalStruct) {
    Program program = parseSource("struct P { uint8_t x; }; void main(void) { struct P v; struct P *p = &v; }");
    EXPECT_EQ(1, program.functions[0].body->statements[0]->type.structId);
    EXPECT_TRUE(program.functions[0].body->statements[1]->type.isPointer);
}

TEST(ParserTest, MemberAccess) {
    EXPECT_EQ("(. a x)", expressionOf("a.x"));
    EXPECT_EQ("(-> a x)", expressionOf("a->x"));
    EXPECT_EQ("(. (index a 2) x)", expressionOf("a[2].x"));
    EXPECT_EQ("(. (* a) x)", expressionOf("(*a).x"));
    EXPECT_EQ("(+ (. a x) (-> b y))", expressionOf("a.x + b->y"));
    EXPECT_EQ("(& (. a x))", expressionOf("&a.x"));
    EXPECT_EQ("(index (. a name) 1)", expressionOf("a.name[1]"));
    EXPECT_EQ("(-> (-> a next) value)", expressionOf("a->next->value"));
}

TEST(ParserTest, MemberAssignmentAndStep) {
    Program program = parseSource(inMain("a.x = 1; a->y += 2; a[3].z++; --a->w;"));
    const std::vector<StmtPtr>& s = program.functions[0].body->statements;
    ASSERT_EQ(4u, s.size());
    EXPECT_EQ(Stmt::ASSIGNMENT, s[0]->kind);
    EXPECT_EQ("(. a x)", toString(*s[0]->target));
    EXPECT_EQ("+=", s[1]->op);
    EXPECT_EQ("(-> a y)", toString(*s[1]->target));
    EXPECT_EQ(Stmt::STEP, s[2]->kind);
    EXPECT_EQ(Stmt::STEP, s[3]->kind);
}

TEST(ParserTest, CastToAStructPointer) {
    Program program = parseSource("struct P { uint8_t x; }; uint8_t v; void main(void) { v = ((struct P *)5)->x; }");
    EXPECT_EQ("(-> (cast struct 1 * 5) x)", toString(*program.functions[0].body->statements[0]->expr));
}

TEST(ParserTest, UnknownStruct) {
    CompileError error = errorOf("struct Nope v; void main(void) { }");
    EXPECT_EQ(1, error.line);
    EXPECT_NE(std::string::npos, error.message.find("Nope")) << error.message;
}

TEST(ParserTest, StructDefinedTwice) {
    EXPECT_NE(std::string::npos, errorOf("struct A { uint8_t a; }; struct A { uint8_t b; }; void main(void) { }").message.find("twice"));
}

TEST(ParserTest, MemberDeclaredTwice) {
    EXPECT_NE(std::string::npos, errorOf("struct A { uint8_t a; uint8_t a; }; void main(void) { }").message.find("twice"));
}

TEST(ParserTest, EmptyStruct) {
    EXPECT_NE(std::string::npos, errorOf("struct A { }; void main(void) { }").message.find("no members"));
}

TEST(ParserTest, StructInsideItselfByValue) {
    EXPECT_NE(std::string::npos, errorOf("struct A { struct A inner; }; void main(void) { }").message.find("pointer"));
}

TEST(ParserTest, StructInAStructByValueIsNotSupportedYet) {
    EXPECT_NE(std::string::npos, errorOf("struct A { uint8_t a; }; struct B { struct A a; }; void main(void) { }").message.find("inside"));
}

TEST(ParserTest, BitFieldsAreNotSupported) {
    EXPECT_NE(std::string::npos, errorOf("struct A { uint8_t a : 3; }; void main(void) { }").message.find("bit field"));
}

TEST(ParserTest, StructPassedOrReturnedByValueIsNotSupported) {
    EXPECT_NE(std::string::npos, errorOf("struct A { uint8_t a; }; void f(struct A v) { } void main(void) { }").message.find("pointer"));
    EXPECT_NE(std::string::npos, errorOf("struct A { uint8_t a; }; struct A f(void) { } void main(void) { }").message.find("pointer"));
}

TEST(ParserTest, MemberNeedsAName) {
    EXPECT_NE(std::string::npos, errorOf(inMain("a = b.;")).message.find("name"));
}

TEST(ParserTest, ForwardDeclaration) {
    Program program = parseSource("struct A; struct B { struct A *a; }; struct A { uint8_t v; struct B *b; }; struct B x; void main(void) { }");
    ASSERT_EQ(2u, program.structs.size());
    EXPECT_EQ(1, program.structs[1].members[0].type.pointeeStruct);
    EXPECT_EQ(2, program.structs[0].size == 2 ? 2 : 0);
}
