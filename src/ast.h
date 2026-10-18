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
	Type() : size(1), isSigned(false), isPointer(false), pointeeSize(0), pointeeSigned(false), structId(0), pointeeStruct(0) {}
	Type(int size, bool isSigned) : size(size), isSigned(isSigned), isPointer(false), pointeeSize(0), pointeeSigned(false), structId(0), pointeeStruct(0) {}
	bool operator==(const Type& other) const
	{
		return size == other.size && isSigned == other.isSigned && isPointer == other.isPointer &&
			pointeeSize == other.pointeeSize && pointeeSigned == other.pointeeSigned &&
			structId == other.structId && pointeeStruct == other.pointeeStruct;
	}
	bool operator!=(const Type& other) const { return !(*this == other); }

	/** What a pointer points to. */
	Type pointee() const
	{
		Type element(pointeeSize, pointeeSigned);
		element.structId = pointeeStruct;
		return element;
	}

	int size;		// in bytes: 1 or 2
	bool isSigned;
	bool isPointer;
	int pointeeSize;	// pointers: the size of the element, 1 or 2 (0 for a struct)
	bool pointeeSigned;
	int structId;		// a struct: its number in Program::structs, counted from 1; otherwise 0
	int pointeeStruct;	// a pointer to a struct: the number of the struct
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
	enum Kind { NUMBER, VARIABLE, UNARY, BINARY, CALL, CAST, INDEX, INDIRECT, MEMBER };
	Expr(Kind kind, int line) : kind(kind), line(line), value(0), parenthesized(false), arrow(false) {}

	Kind kind;
	int line;
	int value;		// NUMBER
	bool parenthesized;	// written in parentheses
	bool arrow;		// MEMBER: left->name, otherwise left.name
	std::string name;	// VARIABLE: the variable, UNARY and BINARY: the operator ("*" and "&" are
				// dereference and address), CALL: the function, MEMBER: the member
	ExprPtr left;		// UNARY: the operand; INDEX: the array or pointer
	ExprPtr right;		// INDEX: the index
	std::vector<ExprPtr> arguments;	// CALL
	Type type;		// CAST: the type it converts to; left is the operand.
				// INDIRECT (made by the code generator, never by the parser): the type of the
				// value in RAM at the address held in the temporary named by `name`
};

struct Stmt
{
	enum Kind { EMPTY, BLOCK, DECLARATION, ASSIGNMENT, STEP, CALL, IF, WHILE, DO_WHILE, FOR, RETURN, BREAK, CONTINUE, SWITCH, CASE, DEFAULT };
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
				// the condition of IF, WHILE, DO_WHILE, FOR (may be empty), the value of SWITCH, the
				// constant of CASE
	std::vector<StmtPtr> statements;	// BLOCK
	StmtPtr init;		// FOR
	StmtPtr step;		// FOR
	StmtPtr body;		// WHILE, DO_WHILE, FOR; the "then" of IF; SWITCH: a BLOCK with CASE and DEFAULT in it
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
	Function() : line(0), returnsValue(false), library(false) {}

	std::string name;
	int line;
	bool returnsValue;	// otherwise void
	Type returnType;	// if it returns a value
	std::vector<Parameter> parameters;
	StmtPtr body;		// a BLOCK, empty for a prototype
	// A function written in assembler (made by the library, never by the parser): the
	// text goes into the program as it is. {name} in it is the register of a parameter
	// or of one of `registers`, which are variables of the function that have to sit in
	// the direct registers. It ends with retp itself.
	std::string assembly;
	std::vector<std::string> registers;
	bool library;		// made by the library or the runtime, not written by the user: no line markers
};

struct Member
{
	std::string name;
	Type type;		// of the member, or of the elements of an array
	int count;		// elements of an array member, 0 if it is none
	int offset;		// bytes from the start of the struct
};

struct StructDefinition
{
	StructDefinition() : line(0), size(0) {}
	std::string name;
	int line;
	std::vector<Member> members;
	int size;		// bytes, the members follow each other
};

struct Program
{
	std::vector<StructDefinition> structs;	// a struct has the number of its place here plus one
	std::vector<StmtPtr> globals;		// DECLARATIONs
	std::vector<Function> functions;	// the definitions
	std::vector<Function> prototypes;	// declarations without a body
};

/** The expression in prefix form, e.g. "(+ a (<< b 2))". For tests and messages. */
std::string toString(const Expr& expr);

#endif /*AST_H_*/
