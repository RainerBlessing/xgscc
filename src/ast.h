#ifndef AST_H_
#define AST_H_
#include <memory>
#include <string>
#include <vector>

/** The integer types: 8 or 16 bits, signed or unsigned. */
struct Type
{
	Type() : size(1), isSigned(false) {}
	Type(int size, bool isSigned) : size(size), isSigned(isSigned) {}
	bool operator==(const Type& other) const { return size == other.size && isSigned == other.isSigned; }
	bool operator!=(const Type& other) const { return !(*this == other); }

	int size;		// in bytes: 1 or 2
	bool isSigned;
};

/** "uint8_t", "int8_t", "uint16_t" or "int16_t". */
std::string typeName(const Type& type);

struct Expr;
struct Stmt;
typedef std::unique_ptr<Expr> ExprPtr;
typedef std::unique_ptr<Stmt> StmtPtr;

struct Expr
{
	enum Kind { NUMBER, VARIABLE, UNARY, BINARY, CALL, CAST };
	Expr(Kind kind, int line) : kind(kind), line(line), value(0) {}

	Kind kind;
	int line;
	int value;		// NUMBER
	std::string name;	// VARIABLE: the variable, UNARY and BINARY: the operator, CALL: the function
	ExprPtr left;		// UNARY: the operand
	ExprPtr right;
	std::vector<ExprPtr> arguments;	// CALL
	Type type;		// CAST: the type it converts to; left is the operand
};

struct Stmt
{
	enum Kind { EMPTY, BLOCK, DECLARATION, ASSIGNMENT, STEP, CALL, IF, WHILE, DO_WHILE, FOR, RETURN, BREAK, CONTINUE };
	Stmt(Kind kind, int line) : kind(kind), line(line), isStatic(false), isConst(false) {}

	Kind kind;
	int line;
	std::string name;	// DECLARATION, ASSIGNMENT, STEP: the variable
	std::string op;		// ASSIGNMENT: "=", "+=", ...; STEP: "++" or "--"
	Type type;		// DECLARATION
	bool isStatic;		// DECLARATION
	bool isConst;		// DECLARATION
	ExprPtr expr;		// the value of DECLARATION (may be empty) and ASSIGNMENT and RETURN (may be empty), the call of CALL,
				// the condition of IF, WHILE, DO_WHILE, FOR (may be empty)
	std::vector<StmtPtr> statements;	// BLOCK
	StmtPtr init;		// FOR
	StmtPtr step;		// FOR
	StmtPtr body;		// WHILE, DO_WHILE, FOR; the "then" of IF
	StmtPtr elseBody;	// IF
};

struct Parameter
{
	std::string name;
	int line;
	Type type;
};

struct Function
{
	Function() : line(0), returnsValue(false) {}

	std::string name;
	int line;
	bool returnsValue;	// otherwise void
	Type returnType;	// if it returns a value
	std::vector<Parameter> parameters;
	StmtPtr body;		// a BLOCK, empty for a prototype
};

struct Program
{
	std::vector<StmtPtr> globals;		// DECLARATIONs
	std::vector<Function> functions;	// the definitions
	std::vector<Function> prototypes;	// declarations without a body
};

/** The expression in prefix form, e.g. "(+ a (<< b 2))". For tests and messages. */
std::string toString(const Expr& expr);

#endif /*AST_H_*/
