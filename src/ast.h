#ifndef AST_H_
#define AST_H_
#include <memory>
#include <string>
#include <vector>

struct Expr;
struct Stmt;
typedef std::unique_ptr<Expr> ExprPtr;
typedef std::unique_ptr<Stmt> StmtPtr;

struct Expr
{
	enum Kind { NUMBER, VARIABLE, UNARY, BINARY };
	Expr(Kind kind, int line) : kind(kind), line(line), value(0) {}

	Kind kind;
	int line;
	int value;		// NUMBER
	std::string name;	// VARIABLE: the variable, UNARY and BINARY: the operator
	ExprPtr left;		// UNARY: the operand
	ExprPtr right;
};

struct Stmt
{
	enum Kind { EMPTY, BLOCK, DECLARATION, ASSIGNMENT, STEP, IF, WHILE, FOR };
	Stmt(Kind kind, int line) : kind(kind), line(line) {}

	Kind kind;
	int line;
	std::string name;	// DECLARATION, ASSIGNMENT, STEP: the variable
	std::string op;		// ASSIGNMENT: "=", "+=", ...; STEP: "++" or "--"
	ExprPtr expr;		// the value of DECLARATION (may be empty) and ASSIGNMENT, the condition of IF, WHILE, FOR (may be empty)
	std::vector<StmtPtr> statements;	// BLOCK
	StmtPtr init;		// FOR
	StmtPtr step;		// FOR
	StmtPtr body;		// WHILE, FOR; the "then" of IF
	StmtPtr elseBody;	// IF
};

struct Function
{
	std::string name;
	int line;
	StmtPtr body;		// a BLOCK
};

struct Program
{
	std::vector<StmtPtr> globals;		// DECLARATIONs
	std::vector<Function> functions;
};

/** The expression in prefix form, e.g. "(+ a (<< b 2))". For tests and messages. */
std::string toString(const Expr& expr);

#endif /*AST_H_*/
