#include "codegen.h"
#include <sstream>
#include <vector>
#include "errors.h"

using namespace std;

// How the code is made
//
// Every expression is computed into W. Variables live at fixed RAM addresses:
// the temporaries of expressions at 0x0A-0x0D, the variables at 0x0E, 0x0F and
// 0x10-0x1F (the start code selects bank 1, so that these are RAM and not the
// special registers that bank 0 shows at 0x10-0x1F).
//
// All values are 8 bit and unsigned, constant expressions too: 2 - 3 is 255.
// A comparison compares with "mov w, a-w": the carry is set if a >= w.
// The jumps of conditions skip over a "jmp" with a bit test of the Z or C flag.

namespace
{
	const int FIRST_TEMPORARY = 0x0a;
	const int TEMPORARIES = 4;
	const int VARIABLE_ADDRESSES[] = {
		0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
	};
	const int VARIABLE_COUNT = sizeof(VARIABLE_ADDRESSES) / sizeof(VARIABLE_ADDRESSES[0]);

	struct Variable
	{
		string label;		// the name in the assembler source
		int address;
	};

	string hexText(int value)
	{
		ostringstream out;
		out << hex << value;
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

	class Generator
	{
	public:
		Generator() : nextVariable(0), temporariesInUse(0), nextLabel(0) {}

		string run(const Program& program, map<string, int>* globals)
		{
			scopes.push_back(map<string, Variable>());
			findMain(program);
			for (size_t i = 0; i < program.globals.size(); i++)
				declareGlobal(*program.globals[i]);
			*globals = globalAddresses;

			statement(*mainFunction->body);

			ostringstream out;
			for (size_t i = 0; i < declarations.size(); i++)
				out << declarations[i] << "\n";
			out << "        org fff\n"
				"        jmp _start\n"
				"        org 0\n"
				"_start:\n"
				"        bank 1\n";
			out << initialization.str();
			out << code.str();
			out << "_exit:\n"
				"        jmp _exit\n";
			return out.str();
		}

	private:
		const Function* mainFunction;
		vector<map<string, Variable> > scopes;
		vector<string> declarations;			// "name equ address"
		map<string, int> globalAddresses;
		map<string, int> usedLabels;
		int nextVariable;
		int temporariesInUse;
		int nextLabel;
		ostringstream initialization;		// start code of the globals
		ostringstream code;

		void findMain(const Program& program)
		{
			mainFunction = NULL;
			for (size_t i = 0; i < program.functions.size(); i++)
			{
				const Function& function = program.functions[i];
				if (function.name != "main")
					throw CompileError(function.line, "function '" + function.name + "' is not supported yet, only main");
				if (mainFunction != NULL)
					throw CompileError(function.line, "main is defined twice");
				mainFunction = &function;
			}
			if (mainFunction == NULL)
				throw CompileError(1, "there is no function main");
		}

		// variables

		Variable declare(const Stmt& declaration)
		{
			map<string, Variable>& scope = scopes.back();
			if (scope.count(declaration.name))
				throw CompileError(declaration.line, "'" + declaration.name + "' is declared twice");
			if (nextVariable >= VARIABLE_COUNT)
				throw CompileError(declaration.line, "out of RAM: more than " + decimal(VARIABLE_COUNT) + " variables");

			Variable variable;
			variable.address = VARIABLE_ADDRESSES[nextVariable++];
			variable.label = "v_" + declaration.name;
			while (usedLabels.count(variable.label))
				variable.label += "_";
			usedLabels[variable.label] = 1;
			declarations.push_back(variable.label + " equ " + hexText(variable.address));
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

		void declareGlobal(const Stmt& declaration)
		{
			Variable variable = declare(declaration);
			globalAddresses[declaration.name] = variable.address;
			int value = 0;
			if (declaration.expr && !fold(*declaration.expr, &value))
				throw CompileError(declaration.line, "the initializer of '" + declaration.name + "' is not constant");
			if (value == 0)
				initialization << "        clr " << variable.label << "\n";
			else
				initialization << "        mov w, #" << hexText(value) << "\n"
					"        mov " << variable.label << ", w\n";
		}

		// temporaries

		int allocateTemporary(int line)
		{
			if (temporariesInUse >= TEMPORARIES)
				throw CompileError(line, "the expression is too complex (more than " + decimal(TEMPORARIES) + " temporaries)");
			return FIRST_TEMPORARY + temporariesInUse++;
		}

		void freeTemporary()
		{
			temporariesInUse--;
		}

		static string temporary(int address)
		{
			return hexText(address);
		}

		// output

		void emit(const string& instruction)
		{
			code << "        " << instruction << "\n";
		}

		void label(const string& name)
		{
			code << name << ":\n";
		}

		string newLabel()
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
				return false;
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

		static string decimal(int value)
		{
			ostringstream out;
			out << value;
			return out.str();
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
			if (value.kind == Expr::VARIABLE)
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
				else if (a.kind == Expr::VARIABLE)
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
			if (a.kind == Expr::VARIABLE)
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

		// conditions

		// W = 0 or 1
		void booleanValue(const Expr& expr)
		{
			int t = allocateTemporary(expr.line);
			string isFalse = newLabel();
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
					string skip = newLabel();
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
				emit(string(stmt.op == "++" ? "inc " : "dec ") + lookup(stmt.name, stmt.line).label);
				break;
			case Stmt::IF:
				ifStatement(stmt);
				break;
			case Stmt::WHILE:
				whileStatement(stmt);
				break;
			case Stmt::FOR:
				forStatement(stmt);
				break;
			}
		}

		void localDeclaration(const Stmt& stmt)
		{
			Variable variable = declare(stmt);
			if (stmt.expr)
				store(variable, *stmt.expr);
		}

		void store(const Variable& variable, const Expr& value)
		{
			int constant;
			if (fold(value, &constant) && constant == 0)
			{
				emit("clr " + variable.label);
				return;
			}
			expression(value);
			emit("mov " + variable.label + ", w");
		}

		void assignment(const Stmt& stmt)
		{
			const Variable& variable = lookup(stmt.name, stmt.line);
			if (stmt.op == "=")
			{
				store(variable, *stmt.expr);
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

		void ifStatement(const Stmt& stmt)
		{
			string otherwise = newLabel();
			jumpIfFalse(*stmt.expr, otherwise);
			statement(*stmt.body);
			if (stmt.elseBody)
			{
				string end = newLabel();
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
			string top = newLabel();
			string end = newLabel();
			label(top);
			jumpIfFalse(*stmt.expr, end);
			statement(*stmt.body);
			emit("jmp " + top);
			label(end);
		}

		void forStatement(const Stmt& stmt)
		{
			scopes.push_back(map<string, Variable>());
			if (stmt.init)
				statement(*stmt.init);
			string top = newLabel();
			string end = newLabel();
			label(top);
			if (stmt.expr)
				jumpIfFalse(*stmt.expr, end);
			statement(*stmt.body);
			if (stmt.step)
				statement(*stmt.step);
			emit("jmp " + top);
			label(end);
			scopes.pop_back();
		}
	};
}

string generateAssembly(const Program& program, map<string, int>* variables)
{
	return Generator().run(program, variables);
}
