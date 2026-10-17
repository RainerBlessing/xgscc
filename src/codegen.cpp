#include "codegen.h"
#include <algorithm>
#include <set>
#include <sstream>
#include <vector>
#include "errors.h"

using namespace std;

// How the code is made
//
// Every expression is computed into W. A function returns its result in W.
//
// There is no data stack: every variable has a fixed RAM address. Globals,
// static variables and the parameters of a function have an address of their
// own. The local variables and the temporaries of the expressions of a function
// are its "frame"; two functions of which neither can call the other (directly
// or through other functions) never run at the same time and share their
// frames. Functions that call each other cannot do so, which is why recursion
// is refused. The addresses are chosen after all code is made: the code uses
// names and the assembler source defines them with "equ".
//
// The RAM is the six direct registers 0x0A-0x0F and the banks 1-7 (0x10-0x7f):
// 118 bytes. An instruction reaches a banked byte as register 0x10-0x1F of the
// bank that FSR selects. The code only says "@bank name" in front of the
// instructions that use a variable; when the addresses are known a pass writes
// "bank n" where the bank is not already selected. The bank is unknown at the
// start of a function and after a label or a call. The most used variables
// (those in loops count more) get the direct registers, which need no bank.
//
// All values are 8 bit and unsigned, constant expressions too: 2 - 3 is 255.
// A comparison compares with "mov w, a-w": the carry is set if a >= w.
// The jumps of conditions skip over a "jmp" with a bit test of the Z or C flag.
//
// Calls: the arguments are stored into the parameters of the callee, "call"
// jumps there and the callee ends with "retp", which also restores the page of
// the caller. The assembler source starts with "autopage on": it puts the page
// instructions where code and callees are on different pages. A callee starts
// with "firsthalf", because a call reaches only the first half of a page.

namespace
{
	const int TEMPORARIES = 4;			// per function
	const int RETURN_STACK = 8;			// entries of the hardware return stack
	const int DIRECT_FIRST = 0x0a;			// the direct registers 0x0A-0x0F
	const int DIRECT_LAST = 0x0f;
	const int BANKED_FIRST = 0x10;			// banks 1-7
	const int BANKED_LAST = 0x7f;

	struct Variable
	{
		Variable() : isConst(false), value(0) {}
		string label;		// the name in the assembler source
		bool isConst;		// a constant, takes no RAM
		int value;
	};

	struct FunctionInfo
	{
		FunctionInfo() : line(0), returnsValue(false), body(NULL), defined(false), temporaries(0) {}

		string name;
		int line;
		bool returnsValue;
		vector<Parameter> parameters;
		const Stmt* body;
		bool defined;
		string entry;		// label of the first instruction
		string exit;		// label of the return
		vector<string> parameterLabels;
		vector<string> localLabels;
		vector<string> staticLabels;
		int temporaries;	// the most that were in use at a time
		map<string, int> calls;	// the functions called -> line of the first call
		ostringstream code;
		ostringstream initialization;	// start code of its static variables
	};

	struct LoopLabels
	{
		string breakLabel;
		string continueLabel;
	};

	string hexText(int value)
	{
		ostringstream out;
		out << hex << value;
		return out.str();
	}

	string decimal(int value)
	{
		ostringstream out;
		out << value;
		return out.str();
	}

	bool isComparison(const string& op)
	{
		return op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=";
	}

	// the instruction that combines W with something: "add", "and", "or", "xor"
	string mnemonic(const string& op)
	{
		if (op == "+") return "add";
		if (op == "&") return "and";
		if (op == "|") return "or";
		return "xor";
	}

	bool hasCall(const Expr& expr)
	{
		if (expr.kind == Expr::CALL)
			return true;
		return (expr.left && hasCall(*expr.left)) || (expr.right && hasCall(*expr.right));
	}

	class Generator
	{
	public:
		Generator() : current(NULL), mainFunction(NULL), loopDepth(0), temporariesInUse(0), nextLabel(0) {}

		string run(const Program& program, map<string, int>* globals)
		{
			scopes.push_back(map<string, Variable>());
			collectFunctions(program);
			for (size_t i = 0; i < program.globals.size(); i++)
				declareGlobal(*program.globals[i]);
			for (size_t i = 0; i < definitionOrder.size(); i++)
				generateFunction(functions[definitionOrder[i]]);

			checkCalls();
			map<string, int> addresses;
			allocate(&addresses);
			for (map<string, string>::const_iterator g = globalLabels.begin(); g != globalLabels.end(); ++g)
				(*globals)[g->first] = addresses[g->second];
			return assembly(addresses);
		}

	private:
		FunctionInfo* current;
		FunctionInfo* mainFunction;
		map<string, FunctionInfo> functions;
		vector<string> definitionOrder;
		vector<map<string, Variable> > scopes;
		vector<string> uniqueLabels;		// globals, in order
		map<string, string> globalLabels;	// name of a global -> label
		set<string> usedLabels;
		vector<LoopLabels> loops;
		set<string> allocatable;		// labels of variables and temporaries
		map<string, int> weights;		// how much each of them is used
		int loopDepth;
		int temporariesInUse;
		int nextLabel;
		ostringstream initialization;		// start code of the globals
		vector<string> reachable;		// functions called from main, main first, in the order found

		// functions

		void collectFunctions(const Program& program)
		{
			for (size_t i = 0; i < program.prototypes.size(); i++)
				declareFunction(program.prototypes[i]);
			for (size_t i = 0; i < program.functions.size(); i++)
			{
				const Function& function = program.functions[i];
				FunctionInfo& info = declareFunction(function);
				if (info.defined)
					throw CompileError(function.line, "function '" + function.name + "' is defined twice");
				info.defined = true;
				info.body = function.body.get();
				info.line = function.line;
				info.parameters = function.parameters;
				definitionOrder.push_back(function.name);
			}
			map<string, FunctionInfo>::iterator found = functions.find("main");
			if (found == functions.end() || !found->second.defined)
				throw CompileError(1, "there is no function main");
			mainFunction = &found->second;
			if (mainFunction->returnsValue || !mainFunction->parameters.empty())
				throw CompileError(mainFunction->line, "main must be void main(void)");
		}

		FunctionInfo& declareFunction(const Function& function)
		{
			map<string, FunctionInfo>::iterator found = functions.find(function.name);
			if (found == functions.end())
			{
				FunctionInfo& info = functions[function.name];
				info.name = function.name;
				info.line = function.line;
				info.returnsValue = function.returnsValue;
				info.parameters = function.parameters;
				if (function.returnsValue)
					requireUint8(function.returnType, function.line);
				for (size_t i = 0; i < function.parameters.size(); i++)
					requireUint8(function.parameters[i].type, function.parameters[i].line);
				info.entry = "f_" + function.name;
				info.exit = function.name == "main" ? "_exit" : "_ret_" + function.name;
				return info;
			}
			FunctionInfo& info = found->second;
			if (info.returnsValue != function.returnsValue || info.parameters.size() != function.parameters.size())
				throw CompileError(function.line, "'" + function.name + "' does not match its earlier declaration");
			return info;
		}

		FunctionInfo& lookupFunction(const string& name, int line)
		{
			map<string, FunctionInfo>::iterator found = functions.find(name);
			if (found == functions.end())
				throw CompileError(line, "unknown function '" + name + "'");
			return found->second;
		}

		// variables

		// a name for a variable, made known to the allocator
		string newLabel(const string& wanted)
		{
			string label = wanted;
			while (usedLabels.count(label))
				label += "_";
			usedLabels.insert(label);
			allocatable.insert(label);
			return label;
		}

		// the label of a variable of the current function, or of a global
		string variableLabel(const string& name) const
		{
			return "v_" + (current != NULL ? current->name + "_" : string()) + name;
		}

		static void requireUint8(const Type& type, int line)
		{
			if (type != Type(1, false))
				throw CompileError(line, "the type " + typeName(type) + " is not supported by the code generator yet");
		}

		Variable declare(const Stmt& declaration, bool isParameter = false)
		{
			requireUint8(declaration.type, declaration.line);
			map<string, Variable>& scope = scopes.back();
			if (scope.count(declaration.name))
				throw CompileError(declaration.line, "'" + declaration.name + "' is declared twice");
			Variable variable;
			if (declaration.isConst)
			{
				int value;
				if (!declaration.expr || !fold(*declaration.expr, &value))
					throw CompileError(declaration.line, "the const '" + declaration.name + "' needs a constant initializer");
				variable.isConst = true;
				variable.value = value;
			}
			else
				variable.label = newLabel(variableLabel(declaration.name));
			(void) isParameter;
			scope[declaration.name] = variable;
			return variable;
		}

		const Variable& lookup(const string& name, int line) const
		{
			for (size_t i = scopes.size(); i > 0; i--)
			{
				map<string, Variable>::const_iterator found = scopes[i - 1].find(name);
				if (found != scopes[i - 1].end())
					return found->second;
			}
			throw CompileError(line, "unknown variable '" + name + "'");
		}

		// a variable that is not a constant, to be changed
		const Variable& lookupForWriting(const string& name, int line) const
		{
			const Variable& variable = lookup(name, line);
			if (variable.isConst)
				throw CompileError(line, "cannot change the const '" + name + "'");
			return variable;
		}

		bool isPlainVariable(const Expr& expr) const
		{
			return expr.kind == Expr::VARIABLE && !lookup(expr.name, expr.line).isConst;
		}

		// start code that gives a variable with its own address its first value
		void initialize(ostringstream& out, const Stmt& declaration, const Variable& variable)
		{
			int value = 0;
			if (declaration.expr && !fold(*declaration.expr, &value))
				throw CompileError(declaration.line, "the initializer of '" + declaration.name + "' is not constant");
			if (value == 0)
				out << instructionLines("clr " + variable.label);
			else
				out << instructionLines("mov w, #" + hexText(value)) << instructionLines("mov " + variable.label + ", w");
		}

		void declareGlobal(const Stmt& declaration)
		{
			Variable variable = declare(declaration);
			if (variable.isConst)
				return;
			uniqueLabels.push_back(variable.label);
			globalLabels[declaration.name] = variable.label;
			initialize(initialization, declaration, variable);
		}

		void localDeclaration(const Stmt& stmt)
		{
			Variable variable = declare(stmt);
			if (variable.isConst)
				return;
			if (stmt.isStatic)
			{
				current->staticLabels.push_back(variable.label);
				initialize(current->initialization, stmt, variable);
				return;
			}
			current->localLabels.push_back(variable.label);
			if (stmt.expr)
				store(variable.label, *stmt.expr);
		}

		// temporaries

		int allocateTemporary(int line)
		{
			if (temporariesInUse >= TEMPORARIES)
				throw CompileError(line, "the expression is too complex (more than " + decimal(TEMPORARIES) + " temporaries)");
			current->temporaries = max(current->temporaries, temporariesInUse + 1);
			allocatable.insert(temporary(temporariesInUse));
			return temporariesInUse++;
		}

		void freeTemporary()
		{
			temporariesInUse--;
		}

		string temporary(int index) const
		{
			return "t_" + current->name + "_" + decimal(index);
		}

		// output

		// The instruction as source lines. An instruction that uses a variable
		// is preceded by "@bank name"; the weight of the variable counts the
		// loops around the use.
		string instructionLines(const string& instruction)
		{
			string lines;
			string used;
			size_t operands = instruction.find(' ');
			for (size_t i = operands == string::npos ? instruction.size() : operands; i < instruction.size();)
			{
				if (!isalnum((unsigned char) instruction[i]) && instruction[i] != '_')
				{
					i++;
					continue;
				}
				size_t end = i;
				while (end < instruction.size() && (isalnum((unsigned char) instruction[end]) || instruction[end] == '_'))
					end++;
				string word = instruction.substr(i, end - i);
				if (allocatable.count(word))
				{
					if (!used.empty() && used != word)
						throw CompileError(0, "internal error: '" + instruction + "' uses two variables");
					used = word;
				}
				i = end;
			}
			if (!used.empty())
			{
				int weight = 1 << (3 * min(loopDepth, 3));
				weights[used] += weight;
				lines += "        @bank " + used + "\n";
			}
			return lines + "        " + instruction + "\n";
		}

		void emit(const string& instruction)
		{
			current->code << instructionLines(instruction);
		}

		void label(const string& name)
		{
			current->code << name << ":\n";
		}

		string newJumpLabel()
		{
			return "_L" + hexText(nextLabel++);
		}

		// constants

		// Computes a constant expression with 8 bit unsigned values.
		bool fold(const Expr& expr, int* value) const
		{
			switch (expr.kind)
			{
			case Expr::NUMBER:
				if (expr.value > 255)
					throw CompileError(expr.line, "the constant " + decimal(expr.value) + " does not fit in 8 bits");
				*value = expr.value;
				return true;
			case Expr::VARIABLE:
			{
				const Variable& variable = lookup(expr.name, expr.line);
				*value = variable.value;
				return variable.isConst;
			}
			case Expr::CALL:
				return false;
			case Expr::CAST:
				throw CompileError(expr.line, "casts are not supported by the code generator yet");
			case Expr::UNARY:
			{
				int operand;
				if (!fold(*expr.left, &operand))
					return false;
				if (expr.name == "-") *value = -operand & 0xff;
				else if (expr.name == "~") *value = ~operand & 0xff;
				else *value = operand == 0;
				return true;
			}
			default:
			{
				int a, b;
				bool left = fold(*expr.left, &a);
				bool right = fold(*expr.right, &b);
				if (!left || !right)
					return false;
				*value = apply(expr.name, a, b);
				return true;
			}
			}
		}

		static int apply(const string& op, int a, int b)
		{
			if (op == "+") return (a + b) & 0xff;
			if (op == "-") return (a - b) & 0xff;
			if (op == "&") return a & b;
			if (op == "|") return a | b;
			if (op == "^") return a ^ b;
			if (op == "<<") return b >= 8 ? 0 : (a << b) & 0xff;
			if (op == ">>") return b >= 8 ? 0 : a >> b;
			if (op == "==") return a == b;
			if (op == "!=") return a != b;
			if (op == "<") return a < b;
			if (op == ">") return a > b;
			if (op == "<=") return a <= b;
			if (op == ">=") return a >= b;
			if (op == "&&") return a && b;
			return a || b;
		}

		bool isSimple(const Expr& expr) const
		{
			int value;
			return expr.kind == Expr::VARIABLE || fold(expr, &value);
		}

		// expressions

		void loadConstant(int value)
		{
			if (value == 0)
				emit("clr w");
			else
				emit("mov w, #" + hexText(value));
		}

		// W = the expression
		void expression(const Expr& expr)
		{
			int value;
			if (fold(expr, &value))
			{
				loadConstant(value);
				return;
			}
			switch (expr.kind)
			{
			case Expr::VARIABLE:
				emit("mov w, " + lookup(expr.name, expr.line).label);
				return;
			case Expr::CALL:
				call(expr, true);
				return;
			case Expr::UNARY:
				unaryExpression(expr);
				return;
			default:
				binaryExpression(expr);
			}
		}

		// W = ~value. "xor w, #ff" cannot be used: its opcode 0xFFF is what the
		// emulator takes for an empty ROM word.
		void complement(const Expr& value, int line)
		{
			if (isPlainVariable(value))
			{
				emit("mov w, /" + lookup(value.name, value.line).label);
				return;
			}
			expression(value);
			int t = allocateTemporary(line);
			emit("mov " + temporary(t) + ", w");
			emit("mov w, /" + temporary(t));
			freeTemporary();
		}

		void unaryExpression(const Expr& expr)
		{
			if (expr.name == "!")
			{
				booleanValue(expr);
				return;
			}
			complement(*expr.left, expr.line);
			if (expr.name == "-")
			{
				// -x is ~x + 1
				int t = allocateTemporary(expr.line);
				emit("mov " + temporary(t) + ", w");
				emit("mov w, #1");
				emit("add w, " + temporary(t));
				freeTemporary();
			}
		}

		void binaryExpression(const Expr& expr)
		{
			const string& op = expr.name;
			if (isComparison(op) || op == "&&" || op == "||")
				booleanValue(expr);
			else if (op == "<<" || op == ">>")
				shift(op, *expr.left, *expr.right, expr.line);
			else if (op == "-")
				subtract(*expr.left, *expr.right, expr.line);
			else
				combine(op, *expr.left, *expr.right, expr.line);
		}

		// W = a op b for + & | ^
		void combine(const string& op, const Expr& a, const Expr& b, int line)
		{
			if (!isSimple(b) && isSimple(a))
			{
				combine(op, b, a, line);		// all four are commutative
				return;
			}
			if (isSimple(b))
			{
				int k;
				if (fold(b, &k))
					combineWithConstant(op, a, k, line);
				else
				{
					expression(a);
					emit(mnemonic(op) + " w, " + lookup(b.name, b.line).label);
				}
				return;
			}
			expression(b);
			int t = allocateTemporary(line);
			emit("mov " + temporary(t) + ", w");
			expression(a);
			emit(mnemonic(op) + " w, " + temporary(t));
			freeTemporary();
		}

		void combineWithConstant(const string& op, const Expr& a, int k, int line)
		{
			if (op == "+")
			{
				if (k == 0)
					expression(a);
				else if (isPlainVariable(a))
				{
					emit("mov w, #" + hexText(k));
					emit("add w, " + lookup(a.name, a.line).label);
				}
				else
				{
					expression(a);
					int t = allocateTemporary(line);
					emit("mov " + temporary(t) + ", w");
					emit("mov w, #" + hexText(k));
					emit("add w, " + temporary(t));
					freeTemporary();
				}
				return;
			}
			if (op == "^" && k == 0xff)
			{
				complement(a, line);
				return;
			}
			expression(a);
			if (op == "&" && k == 0)
				emit("clr w");		// "and w, #0" would and with FSR
			else
				emit(mnemonic(op) + " w, #" + hexText(k));
		}

		// W = a - b; the carry is set if a >= b
		void subtract(const Expr& a, const Expr& b, int line)
		{
			string from;
			bool temporaryUsed = false;
			if (isPlainVariable(a))
				from = lookup(a.name, a.line).label;
			else
			{
				expression(a);
				int t = allocateTemporary(line);
				emit("mov " + temporary(t) + ", w");
				from = temporary(t);
				temporaryUsed = true;
			}
			expression(b);
			emit("mov w, " + from + "-w");
			if (temporaryUsed)
				freeTemporary();
		}

		// W = value << count or value >> count, the count is constant
		void shift(const string& op, const Expr& value, const Expr& countExpr, int line)
		{
			int count;
			if (!fold(countExpr, &count))
				throw CompileError(line, "the shift count must be constant");
			expression(value);
			if (count == 0)
				return;
			if (count >= 8)
			{
				emit("clr w");
				return;
			}
			int t = allocateTemporary(line);
			emit("mov " + temporary(t) + ", w");
			for (int i = 0; i < count; i++)
			{
				emit("clrb status.0");
				emit(string(op == "<<" ? "rl " : "rr ") + temporary(t));
			}
			emit("mov w, " + temporary(t));
			freeTemporary();
		}

		// calls

		// W = the result of the function
		void call(const Expr& expr, bool valueNeeded)
		{
			if (expr.name == "main")
				throw CompileError(expr.line, "main cannot be called");
			FunctionInfo& callee = lookupFunction(expr.name, expr.line);
			if (expr.arguments.size() != callee.parameters.size())
				throw CompileError(expr.line, "function '" + callee.name + "' takes " + decimal((int) callee.parameters.size()) +
					" argument(s), not " + decimal((int) expr.arguments.size()));
			if (valueNeeded && !callee.returnsValue)
				throw CompileError(expr.line, "function '" + callee.name + "' returns no value");
			if (!current->calls.count(callee.name))
				current->calls[callee.name] = expr.line;
			ensureParameterLabels(callee);

			// An argument that contains a call may run this very function and
			// write its parameters: the arguments before it wait in temporaries.
			size_t count = expr.arguments.size();
			vector<int> held(count, -1);
			for (size_t i = 0; i < count; i++)
			{
				bool laterCall = false;
				for (size_t j = i + 1; j < count; j++)
					laterCall = laterCall || hasCall(*expr.arguments[j]);
				if (laterCall)
				{
					expression(*expr.arguments[i]);
					held[i] = allocateTemporary(expr.line);
					emit("mov " + temporary(held[i]) + ", w");
				}
				else
					store(callee.parameterLabels[i], *expr.arguments[i]);
			}
			for (size_t i = 0; i < count; i++)
			{
				if (held[i] >= 0)
				{
					emit("mov w, " + temporary(held[i]));
					emit("mov " + callee.parameterLabels[i] + ", w");
				}
			}
			for (size_t i = 0; i < count; i++)
			{
				if (held[i] >= 0)
					freeTemporary();
			}
			emit("call " + callee.entry);
		}

		// The parameters of a function have names before the function is made.
		void ensureParameterLabels(FunctionInfo& function)
		{
			if (function.parameterLabels.size() == function.parameters.size())
				return;
			function.parameterLabels.clear();
			for (size_t i = 0; i < function.parameters.size(); i++)
				function.parameterLabels.push_back(parameterLabel(function, function.parameters[i].name));
		}

		string parameterLabel(const FunctionInfo& function, const string& name)
		{
			string wanted = "v_" + function.name + "_" + name;
			map<string, string>::const_iterator found = parameterNames.find(wanted);
			if (found != parameterNames.end())
				return found->second;
			string label = newLabel(wanted);
			parameterNames[wanted] = label;
			return label;
		}

		map<string, string> parameterNames;

		// conditions

		// W = 0 or 1
		void booleanValue(const Expr& expr)
		{
			int t = allocateTemporary(expr.line);
			string isFalse = newJumpLabel();
			emit("clr " + temporary(t));
			jumpIfFalse(expr, isFalse);
			emit("inc " + temporary(t));
			label(isFalse);
			emit("mov w, " + temporary(t));
			freeTemporary();
		}

		void jumpIfFalse(const Expr& expr, const string& target) { branch(false, expr, target); }
		void jumpIfTrue(const Expr& expr, const string& target) { branch(true, expr, target); }

		// jumps to target if the expression is true (or false)
		void branch(bool onTrue, const Expr& expr, const string& target)
		{
			int value;
			if (fold(expr, &value))
			{
				if ((value != 0) == onTrue)
					emit("jmp " + target);
				return;
			}
			if (expr.kind == Expr::UNARY && expr.name == "!")
			{
				branch(!onTrue, *expr.left, target);
				return;
			}
			if (expr.kind == Expr::BINARY && (expr.name == "&&" || expr.name == "||"))
			{
				// "a && b" is false if a or b is false; "a || b" is true if a or b is true
				bool shortCircuits = (expr.name == "&&") != onTrue;
				if (shortCircuits)
				{
					branch(onTrue, *expr.left, target);
					branch(onTrue, *expr.right, target);
				}
				else
				{
					string skip = newJumpLabel();
					branch(!onTrue, *expr.left, skip);
					branch(onTrue, *expr.right, target);
					label(skip);
				}
				return;
			}
			if (expr.kind == Expr::BINARY && isComparison(expr.name))
			{
				compareAndJump(onTrue, expr, target);
				return;
			}
			expression(expr);
			emit("or w, #0");		// the zero flag follows W
			// Z is set if the value is 0, i.e. false
			emit(onTrue ? "sb status.2" : "snb status.2");
			emit("jmp " + target);
		}

		void compareAndJump(bool onTrue, const Expr& expr, const string& target)
		{
			const string& op = expr.name;
			if (op == "==" || op == "!=")
			{
				combine("^", *expr.left, *expr.right, expr.line);		// Z is set if equal
				bool jumpWhenEqual = (op == "==") == onTrue;
				emit(jumpWhenEqual ? "snb status.2" : "sb status.2");
				emit("jmp " + target);
				return;
			}
			// a > b is b < a, a <= b is b >= a
			bool swapped = op == ">" || op == "<=";
			const Expr& a = swapped ? *expr.right : *expr.left;
			const Expr& b = swapped ? *expr.left : *expr.right;
			bool isLess = op == "<" || op == ">";
			subtract(a, b, expr.line);		// carry set if a >= b
			// "<" is true if the carry is clear, ">=" if it is set
			bool jumpWhenCarry = isLess != onTrue;
			emit(jumpWhenCarry ? "snb status.0" : "sb status.0");
			emit("jmp " + target);
		}

		// statements

		void generateFunction(FunctionInfo& function)
		{
			current = &function;
			temporariesInUse = 0;
			loopDepth = 0;
			loops.clear();
			ensureParameterLabels(function);

			scopes.push_back(map<string, Variable>());
			for (size_t i = 0; i < function.parameters.size(); i++)
			{
				const Parameter& parameter = function.parameters[i];
				if (scopes.back().count(parameter.name))
					throw CompileError(parameter.line, "'" + parameter.name + "' is declared twice");
				Variable variable;
				variable.label = function.parameterLabels[i];
				scopes.back()[parameter.name] = variable;
			}
			if (&function != mainFunction)
				label(function.entry);
			for (size_t i = 0; i < function.body->statements.size(); i++)
				statement(*function.body->statements[i]);
			if (&function != mainFunction)
			{
				label(function.exit);
				emit("retp");
			}
			scopes.pop_back();
			current = NULL;
		}

		void statement(const Stmt& stmt)
		{
			switch (stmt.kind)
			{
			case Stmt::EMPTY:
				break;
			case Stmt::BLOCK:
				scopes.push_back(map<string, Variable>());
				for (size_t i = 0; i < stmt.statements.size(); i++)
					statement(*stmt.statements[i]);
				scopes.pop_back();
				break;
			case Stmt::DECLARATION:
				localDeclaration(stmt);
				break;
			case Stmt::ASSIGNMENT:
				assignment(stmt);
				break;
			case Stmt::STEP:
				emit(string(stmt.op == "++" ? "inc " : "dec ") + lookupForWriting(stmt.name, stmt.line).label);
				break;
			case Stmt::CALL:
				call(*stmt.expr, false);
				break;
			case Stmt::IF:
				ifStatement(stmt);
				break;
			case Stmt::WHILE:
				whileStatement(stmt);
				break;
			case Stmt::DO_WHILE:
				doStatement(stmt);
				break;
			case Stmt::FOR:
				forStatement(stmt);
				break;
			case Stmt::RETURN:
				returnStatement(stmt);
				break;
			case Stmt::BREAK:
			case Stmt::CONTINUE:
				jumpOutOfLoop(stmt);
				break;
			}
		}

		// label = the value
		void store(const string& target, const Expr& value)
		{
			int constant;
			if (fold(value, &constant) && constant == 0)
			{
				emit("clr " + target);
				return;
			}
			expression(value);
			emit("mov " + target + ", w");
		}

		void assignment(const Stmt& stmt)
		{
			const Variable& variable = lookupForWriting(stmt.name, stmt.line);
			if (stmt.op == "=")
			{
				store(variable.label, *stmt.expr);
				return;
			}
			// a op= b is a = a op b
			Expr self(Expr::VARIABLE, stmt.line);
			self.name = stmt.name;
			string op = stmt.op.substr(0, stmt.op.size() - 1);
			if (op == "<<" || op == ">>")
				shift(op, self, *stmt.expr, stmt.line);
			else if (op == "-")
				subtract(self, *stmt.expr, stmt.line);
			else
				combine(op, self, *stmt.expr, stmt.line);
			emit("mov " + variable.label + ", w");
		}

		void returnStatement(const Stmt& stmt)
		{
			if (current->returnsValue && !stmt.expr)
				throw CompileError(stmt.line, "'return' needs a value in function '" + current->name + "'");
			if (!current->returnsValue && stmt.expr)
				throw CompileError(stmt.line, "'return' with a value in the void function '" + current->name + "'");
			if (stmt.expr)
				expression(*stmt.expr);
			emit("jmp " + current->exit);
		}

		void jumpOutOfLoop(const Stmt& stmt)
		{
			bool isBreak = stmt.kind == Stmt::BREAK;
			if (loops.empty())
				throw CompileError(stmt.line, string("'") + (isBreak ? "break" : "continue") + "' is not inside a loop");
			emit("jmp " + (isBreak ? loops.back().breakLabel : loops.back().continueLabel));
		}

		void ifStatement(const Stmt& stmt)
		{
			string otherwise = newJumpLabel();
			jumpIfFalse(*stmt.expr, otherwise);
			statement(*stmt.body);
			if (stmt.elseBody)
			{
				string end = newJumpLabel();
				emit("jmp " + end);
				label(otherwise);
				statement(*stmt.elseBody);
				label(end);
			}
			else
				label(otherwise);
		}

		void whileStatement(const Stmt& stmt)
		{
			string top = newJumpLabel();
			string end = newJumpLabel();
			label(top);
			loopDepth++;
			jumpIfFalse(*stmt.expr, end);
			LoopLabels labels = { end, top };
			loops.push_back(labels);
			statement(*stmt.body);
			loops.pop_back();
			loopDepth--;
			emit("jmp " + top);
			label(end);
		}

		void doStatement(const Stmt& stmt)
		{
			string top = newJumpLabel();
			string next = newJumpLabel();
			string end = newJumpLabel();
			label(top);
			loopDepth++;
			LoopLabels labels = { end, next };
			loops.push_back(labels);
			statement(*stmt.body);
			loops.pop_back();
			label(next);
			jumpIfTrue(*stmt.expr, top);
			loopDepth--;
			label(end);
		}

		void forStatement(const Stmt& stmt)
		{
			scopes.push_back(map<string, Variable>());
			if (stmt.init)
				statement(*stmt.init);
			string top = newJumpLabel();
			string next = newJumpLabel();
			string end = newJumpLabel();
			label(top);
			loopDepth++;
			if (stmt.expr)
				jumpIfFalse(*stmt.expr, end);
			LoopLabels labels = { end, next };
			loops.push_back(labels);
			statement(*stmt.body);
			loops.pop_back();
			label(next);
			if (stmt.step)
				statement(*stmt.step);
			loopDepth--;
			emit("jmp " + top);
			label(end);
			scopes.pop_back();
		}

		// what the calls make of the program

		void checkCalls()
		{
			vector<string> path;
			set<string> done;
			visit("main", 0, &path, &done);
			if (!reachable.empty())
				checkDepth();
		}

		// finds the functions that main can reach, refuses recursion
		void visit(const string& name, int callLine, vector<string>* path, set<string>* done)
		{
			FunctionInfo& function = functions[name];
			if (!function.defined)
				throw CompileError(callLine, "function '" + name + "' is declared but not defined");
			vector<string>::iterator onPath = find(path->begin(), path->end(), name);
			if (onPath != path->end())
			{
				string cycle;
				for (; onPath != path->end(); ++onPath)
					cycle += *onPath + " -> ";
				throw CompileError(callLine, "recursion is not supported: " + cycle + name);
			}
			if (done->count(name))
				return;
			path->push_back(name);
			reachable.push_back(name);
			for (map<string, int>::const_iterator call = function.calls.begin(); call != function.calls.end(); ++call)
				visit(call->first, call->second, path, done);
			path->pop_back();
			done->insert(name);
		}

		// the most calls inside each other: main -> f -> g is 2
		int depthOf(const string& name, map<string, int>* depths, map<string, string>* deepest)
		{
			map<string, int>::const_iterator known = depths->find(name);
			if (known != depths->end())
				return known->second;
			int best = 0;
			const map<string, int>& calls = functions[name].calls;
			for (map<string, int>::const_iterator call = calls.begin(); call != calls.end(); ++call)
			{
				int depth = 1 + depthOf(call->first, depths, deepest);
				if (depth > best)
				{
					best = depth;
					(*deepest)[name] = call->first;
				}
			}
			(*depths)[name] = best;
			return best;
		}

		void checkDepth()
		{
			map<string, int> depths;
			map<string, string> deepest;
			int depth = depthOf("main", &depths, &deepest);
			if (depth <= RETURN_STACK)
				return;
			string chain = "main";
			string last = "main";
			for (string name = "main"; deepest.count(name); name = deepest[name])
			{
				last = deepest[name];
				chain += " -> " + last;
			}
			int line = functions["main"].calls[deepest["main"]];
			throw CompileError(line, "the calls are " + decimal(depth) + " deep, the return stack has " + decimal(RETURN_STACK) +
				" entries: " + chain);
		}

		// memory

		void descendants(const string& name, set<string>* found)
		{
			const map<string, int>& calls = functions[name].calls;
			for (map<string, int>::const_iterator call = calls.begin(); call != calls.end(); ++call)
			{
				if (found->insert(call->first).second)
					descendants(call->first, found);
			}
		}

		static int frameSize(const FunctionInfo& function)
		{
			return (int) function.localLabels.size() + function.temporaries;
		}

		// Gives every label its RAM address.
		void allocate(map<string, int>* addresses)
		{
			// the places: every variable with an address of its own, and the
			// places of the frames, which functions share
			vector<vector<string> > places;
			for (size_t i = 0; i < uniqueLabels.size(); i++)
				places.push_back(vector<string>(1, uniqueLabels[i]));
			for (size_t i = 0; i < reachable.size(); i++)
			{
				const FunctionInfo& function = functions[reachable[i]];
				for (size_t p = 0; p < function.parameterLabels.size(); p++)
					places.push_back(vector<string>(1, function.parameterLabels[p]));
				for (size_t p = 0; p < function.staticLabels.size(); p++)
					places.push_back(vector<string>(1, function.staticLabels[p]));
			}
			size_t uniquePlaces = places.size();

			// Callers come before the functions they call: the frame of a function
			// starts behind the frames of all functions that can call it.
			vector<string> order = reachable;
			map<string, set<string> > reach;
			for (size_t i = 0; i < order.size(); i++)
				descendants(order[i], &reach[order[i]]);
			map<string, int> base;
			int frames = 0;
			for (size_t pass = 0; pass < order.size(); pass++)
			{
				for (size_t i = 0; i < order.size(); i++)
				{
					int start = 0;
					for (size_t j = 0; j < order.size(); j++)
					{
						if (reach[order[j]].count(order[i]) && base.count(order[j]))
							start = max(start, base[order[j]] + frameSize(functions[order[j]]));
					}
					base[order[i]] = start;
				}
			}
			for (size_t i = 0; i < order.size(); i++)
				frames = max(frames, base[order[i]] + frameSize(functions[order[i]]));
			places.resize(uniquePlaces + frames);
			for (size_t i = 0; i < order.size(); i++)
			{
				const FunctionInfo& function = functions[order[i]];
				size_t place = uniquePlaces + base[order[i]];
				for (size_t l = 0; l < function.localLabels.size(); l++)
					places[place++].push_back(function.localLabels[l]);
				for (int t = 0; t < function.temporaries; t++)
					places[place++].push_back("t_" + function.name + "_" + decimal(t));
			}

			int available = (DIRECT_LAST - DIRECT_FIRST + 1) + (BANKED_LAST - BANKED_FIRST + 1);
			int needed = (int) places.size();
			if (needed > available)
				throw CompileError(mainFunction->line, "out of RAM: " + decimal(needed) + " bytes needed, " + decimal(available) +
					" available (" + decimal((int) uniquePlaces) + " for globals, parameters and static variables, " +
					decimal(frames) + " for local variables and temporaries)");

			// the most used places first: they get the direct registers, then the first bank
			vector<int> addressesInOrder;
			for (int a = DIRECT_FIRST; a <= DIRECT_LAST; a++)
				addressesInOrder.push_back(a);
			for (int a = BANKED_FIRST; a <= BANKED_LAST; a++)
				addressesInOrder.push_back(a);
			vector<pair<int, size_t> > byUse;		// -weight, place: sorts the heaviest first, stable
			for (size_t p = 0; p < places.size(); p++)
			{
				int weight = 0;
				for (size_t l = 0; l < places[p].size(); l++)
					weight += weights[places[p][l]];
				byUse.push_back(make_pair(-weight, p));
			}
			sort(byUse.begin(), byUse.end());
			for (size_t rank = 0; rank < byUse.size(); rank++)
			{
				const vector<string>& labels = places[byUse[rank].second];
				for (size_t l = 0; l < labels.size(); l++)
					(*addresses)[labels[l]] = addressesInOrder[rank];
			}
		}

		// The register an instruction names for the RAM address: banked bytes
		// are 0x10-0x1F of the selected bank.
		static int registerOf(int address)
		{
			return address < BANKED_FIRST ? address : BANKED_FIRST | (address & 0xf);
		}

		// Replaces "@bank name" by "bank n" where the bank is not selected yet.
		// After reset FSR selects bank 0. The bank is not known after a label
		// (the code can be reached from elsewhere) and after a call.
		static string resolveBanks(const string& text, const map<string, int>& addresses)
		{
			istringstream in(text);
			ostringstream out;
			string line;
			string previous;
			int bank = -1;
			while (getline(in, line))
			{
				size_t first = line.find_first_not_of(' ');
				string trimmed = first == string::npos ? "" : line.substr(first);
				if (trimmed.compare(0, 6, "@bank ") == 0)
				{
					if (previous.compare(0, 3, "sb ") == 0 || previous.compare(0, 4, "snb ") == 0)
						throw CompileError(0, "internal error: a bank instruction would sit between a skip and its instruction");
					int address = addresses.at(trimmed.substr(6));
					if (address >= BANKED_FIRST && bank != address / 16)
					{
						bank = address / 16;
						out << "        bank " << hexText(bank) << "\n";
					}
					continue;
				}
				if (!trimmed.empty() && trimmed[trimmed.size() - 1] == ':')
					bank = trimmed == "_start:" ? 0 : -1;
				else if (trimmed.compare(0, 5, "call ") == 0)
				{
					out << line << "\n";
					bank = -1;
					previous = trimmed;
					continue;
				}
				out << line << "\n";
				previous = trimmed;
			}
			return out.str();
		}

		string assembly(const map<string, int>& addresses)
		{
			ostringstream out;
			for (map<string, int>::const_iterator a = addresses.begin(); a != addresses.end(); ++a)
				out << a->first << " equ " << hexText(registerOf(a->second)) << "\n";
			ostringstream body;
			body << "        org fff\n"
				"        jmp _start\n"
				"        org 0\n"
				"        autopage on\n"
				"_start:\n";
			body << initialization.str();
			for (size_t i = 0; i < reachable.size(); i++)
				body << functions[reachable[i]].initialization.str();
			body << mainFunction->code.str();
			body << "_exit:\n"
				"        jmp _exit\n";
			for (size_t i = 0; i < definitionOrder.size(); i++)
			{
				const FunctionInfo& function = functions[definitionOrder[i]];
				if (&function == mainFunction || find(reachable.begin(), reachable.end(), function.name) == reachable.end())
					continue;
				body << "        firsthalf\n" << function.code.str();
			}
			out << resolveBanks(body.str(), addresses);
			return out.str();
		}
	};
}

string generateAssembly(const Program& program, map<string, int>* variables)
{
	return Generator().run(program, variables);
}
