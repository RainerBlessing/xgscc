#ifndef AST_H_
#define AST_H_
#include <memory>
#include <string>
#include <vector>

/**
 * The integer types: 8 or 16 bits, signed or unsigned, and pointers to them.
 * A pointer is the RAM address, one byte, unsigned.
 */
struct Type
{
	Type() : size(1), isSigned(false), isPointer(false), pointeeSize(0), pointeeSigned(false) {}
	Type(int size, bool isSigned) : size(size), isSigned(isSigned), isPointer(false), pointeeSize(0), pointeeSigned(false) {}
	bool operator==(const Type& other) const
	{
		return size == other.size && isSigned == other.isSigned && isPointer == other.isPointer &&
			pointeeSize == other.pointeeSize && pointeeSigned == other.pointeeSigned;
	}
	bool operator!=(const Type& other) const { return !(*this == other); }

	/** What a pointer points to. */
	Type pointee() const { return Type(pointeeSize, pointeeSigned); }

	int size;		// in bytes: 1 or 2
	bool isSigned;
	bool isPointer;
	int pointeeSize;	// pointers: the size of the element, 1 or 2
	bool pointeeSigned;
};

/** The type of a pointer to elements of the given type. */
Type pointerTo(const Type& element);

/** "uint8_t", "int8_t", "uint16_t", "int16_t" or one of them with " *". */
std::string typeName(const Type& type);

struct Expr;
struct Stmt;
typedef std::unique_ptr<Expr> ExprPtr;
typedef std::unique_ptr<Stmt> StmtPtr;

struct Expr
{
	enum Kind { NUMBER, VARIABLE, UNARY, BINARY, CALL, CAST, INDEX };
	Expr(Kind kind, int line) : kind(kind), line(line), value(0), parenthesized(false) {}

	Kind kind;
	int line;
	int value;		// NUMBER
	bool parenthesized;	// written in parentheses
	std::string name;	// VARIABLE: the variable, UNARY and BINARY: the operator ("*" and "&" are
				// dereference and address), CALL: the function
	ExprPtr left;		// UNARY: the operand; INDEX: the array or pointer
	ExprPtr right;		// INDEX: the index
	std::vector<ExprPtr> arguments;	// CALL
	Type type;		// CAST: the type it converts to; left is the operand
};

struct Stmt
{
	enum Kind { EMPTY, BLOCK, DECLARATION, ASSIGNMENT, STEP, CALL, IF, WHILE, DO_WHILE, FOR, RETURN, BREAK, CONTINUE };
	Stmt(Kind kind, int line) : kind(kind), line(line), arraySize(0), isStatic(false), isConst(false) {}

	Kind kind;
	int line;
	std::string name;	// DECLARATION, ASSIGNMENT, STEP: the variable
	ExprPtr target;		// ASSIGNMENT, STEP: what is changed if it is no plain variable (*p, a[i])
	int arraySize;		// DECLARATION: the number of elements of an array, 0 if it is none
	std::vector<ExprPtr> elements;	// DECLARATION of an array: the initializers
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
