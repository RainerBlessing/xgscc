#include "codegen.h"
#include <algorithm>
#include <set>
#include <sstream>
#include <vector>
#include "errors.h"

using namespace std;

// How the code is made
//
// Every expression is computed into W. A value of 16 bits has its high byte in
// the register acc_hi. A function returns its result like that.
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
// Types: uint8_t, int8_t, uint16_t, int16_t. The type of an operation is the
// larger of the types of its operands, unsigned if one of them is and the sizes
// are equal. An 8 bit operation stays 8 bit (200 + 100 is 44 even if it is
// stored in a 16 bit variable); to get 300 one operand is cast. A constant takes
// the type of the other operand if it fits, otherwise the operation is wider.
// Constants are computed exactly.
//
// A comparison compares with "mov w, a-w": the carry is set if a >= w. Signed
// values are compared after turning their sign bit (the top bit is flipped).
// The jumps of conditions skip over a "jmp" with a bit test of the Z or C flag.
// Instructions with a constant are not used for 16 bit values: they work on
// registers, and "xor w, #ff" (the opcode 0xFFF, which the emulator takes for an
// empty ROM word) never comes up.
//
// Calls: the arguments are stored into the parameters of the callee, "call"
// jumps there and the callee ends with "retp", which also restores the page of
// the caller. The assembler source starts with "autopage on": it puts the page
// instructions where code and callees are on different pages. A callee starts
// with "firsthalf", because a call reaches only the first half of a page.

namespace
{
	const int TEMPORARIES = 12;			// bytes per function
	const int RETURN_STACK = 8;			// entries of the hardware return stack
	const int DIRECT_FIRST = 0x0a;			// the direct registers 0x0A-0x0F
	const int DIRECT_LAST = 0x0f;
	const int BANKED_FIRST = 0x10;			// banks 1-7
	const int BANKED_LAST = 0x7f;
	const char* const ACC_HIGH = "acc_hi";		// the high byte of a 16 bit result
	const char* const ACC_LOW = "acc_lo";		// holds a value while FSR points somewhere

	const Type U8(1, false);
	const Type I8(1, true);
	const Type U16(2, false);
	const Type I16(2, true);

	struct Variable
	{
		Variable() : isConst(false), value(0), isArray(false), count(0) {}
		Type type;		// of an array: of its elements
		string lo;		// the names in the assembler source
		string hi;		// of a 16 bit variable
		bool isConst;		// a constant, takes no RAM
		long value;
		bool isArray;		// lo is the physical address of the first element
		int count;		// elements of an array
	};

	// RAM that has to be one run of addresses: an array, or a 16 bit variable whose address is taken
	struct Block
	{
		string label;		// an array: its name; a 16 bit variable: its low byte
		string high;		// a 16 bit variable: its high byte
		int bytes;
	};

	struct ParameterLabels
	{
		string lo;
		string hi;
	};

	struct FunctionInfo
	{
		FunctionInfo() : line(0), returnsValue(false), body(NULL), defined(false), temporaries(0) {}

		string name;
		int line;
		bool returnsValue;
		Type returnType;
		vector<Parameter> parameters;
		const Stmt* body;
		bool defined;
		string entry;		// label of the first instruction
		string exit;		// label of the return
		vector<ParameterLabels> parameterLabels;
		vector<string> localLabels;
		vector<string> staticLabels;
		vector<Block> blocks;	// arrays: they keep their place, the frames of functions are shared
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

	// where a 16 bit (or 8 bit) value that takes part in an operation is
	struct Operand
	{
		Operand() : temporaries(0) {}
		string lo;
		string hi;
		int temporaries;	// temporaries to free when the operation is done
	};

	string hexText(long value)
	{
		ostringstream out;
		out << hex << value;
		return out.str();
	}

	string decimal(long value)
	{
		ostringstream out;
		out << value;
		return out.str();
	}

	bool isComparison(const string& op)
	{
		return op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=";
	}

	bool isLogical(const string& op)
	{
		return op == "&&" || op == "||";
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

	// the value as the type holds it: 300 as uint8_t is 44, 200 as int8_t is -56
	long wrapTo(long value, const Type& type)
	{
		if (type.size == 1)
		{
			long byte = value & 0xff;
			return type.isSigned && byte > 127 ? byte - 256 : byte;
		}
		long word = value & 0xffff;
		return type.isSigned && word > 32767 ? word - 65536 : word;
	}

	// negative values are allowed for unsigned types, and values up to the
	// largest unsigned one for signed types: -1 for uint8_t and 200 for int8_t
	bool fitsLoosely(long value, const Type& type)
	{
		return type.size == 1 ? value >= -128 && value <= 255 : value >= -32768 && value <= 65535;
	}

	bool fitsExactly(long value, const Type& type)
	{
		if (type.size == 1)
			return type.isSigned ? value >= -128 && value <= 127 : value >= 0 && value <= 255;
		return type.isSigned ? value >= -32768 && value <= 32767 : value >= 0 && value <= 65535;
	}

	// the smallest type for a constant
	Type constantType(long value, int line)
	{
		if (value >= 0 && value <= 255) return U8;
		if (value >= -128 && value < 0) return I8;
		if (value >= 256 && value <= 65535) return U16;
		if (value >= -32768 && value < -128) return I16;
		throw CompileError(line, "the constant " + decimal(value) + " does not fit in 16 bits");
	}

	// the type of an operation on two values: the larger one, unsigned wins a tie
	Type commonType(const Type& a, const Type& b)
	{
		if (a.size != b.size)
			return a.size > b.size ? a : b;
		return Type(a.size, a.isSigned && b.isSigned);
	}

	// An exact comparison of a constant with a value needs a type that holds
	// both. Where there is none (a negative constant against uint16_t) it is the
	// type of the value.
	Type holdingBoth(const Type& type, long constant)
	{
		static const Type candidates[] = { U8, I8, U16, I16 };
		long smallest = type.isSigned ? (type.size == 1 ? -128 : -32768) : 0;
		long largest = type.isSigned ? (type.size == 1 ? 127 : 32767) : (type.size == 1 ? 255 : 65535);
		for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
		{
			const Type& candidate = candidates[i];
			if (fitsExactly(smallest, candidate) && fitsExactly(largest, candidate) && fitsExactly(constant, candidate))
				return candidate;
		}
		return type;
	}

	class Generator
	{
	public:
		Generator() : current(NULL), mainFunction(NULL), usesHighByte(false), usesLowByte(false), loopDepth(0), temporariesInUse(0), nextLabel(0)
		{
			allocatable.insert(ACC_HIGH);
			allocatable.insert(ACC_LOW);
		}

		string run(const Program& program, map<string, int>* globals, map<string, int>* highBytes)
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
			for (map<string, string>::const_iterator g = globalHighLabels.begin(); g != globalHighLabels.end(); ++g)
				(*highBytes)[g->first] = addresses[g->second];
			return assembly(addresses);
		}

	private:
		FunctionInfo* current;
		FunctionInfo* mainFunction;
		map<string, FunctionInfo> functions;
		vector<string> definitionOrder;
		vector<map<string, Variable> > scopes;
		vector<string> uniqueLabels;		// globals, in order
		map<string, string> globalLabels;	// name of a global -> label of its low byte
		map<string, string> globalHighLabels;
		set<string> usedLabels;
		vector<LoopLabels> loops;
		set<string> allocatable;		// labels of variables and temporaries
		map<string, int> weights;		// how much each of them is used
		bool usesHighByte;
		bool usesLowByte;
		map<string, string> addressTaken;	// low byte of a variable whose address is taken -> its high byte, if it has one
		vector<Block> globalBlocks;
		set<string> arrayLabels;		// the addresses of arrays are no registers
		int loopDepth;
		int temporariesInUse;
		int nextLabel;
		ostringstream initialization;		// start code of the globals
		vector<string> reachable;		// functions called from main, main first, in the order found
		map<string, ParameterLabels> parameterNames;

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
				info.returnType = function.returnType;
				info.parameters = function.parameters;
				info.entry = "f_" + function.name;
				info.exit = function.name == "main" ? "_exit" : "_ret_" + function.name;
				return info;
			}
			FunctionInfo& info = found->second;
			bool same = info.returnsValue == function.returnsValue && info.parameters.size() == function.parameters.size();
			same = same && (!info.returnsValue || info.returnType == function.returnType);
			for (size_t i = 0; same && i < info.parameters.size(); i++)
				same = info.parameters[i].type == function.parameters[i].type;
			if (!same)
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

		// a name that is no register: the address of an array
		string newPlainLabel(const string& wanted)
		{
			string label = wanted;
			while (usedLabels.count(label))
				label += "_";
			usedLabels.insert(label);
			return label;
		}

		Variable declare(const Stmt& declaration)
		{
			map<string, Variable>& scope = scopes.back();
			if (scope.count(declaration.name))
				throw CompileError(declaration.line, "'" + declaration.name + "' is declared twice");
			Variable variable;
			variable.type = declaration.type;
			if (declaration.isConst && declaration.arraySize != 0)
				throw CompileError(declaration.line, "const arrays (tables in ROM) are not supported by the code generator yet");
			if (declaration.isConst)
			{
				long value;
				if (!declaration.expr || !fold(*declaration.expr, &value))
					throw CompileError(declaration.line, "the const '" + declaration.name + "' needs a constant initializer");
				checkFits(value, declaration.type, declaration.line);
				variable.isConst = true;
				variable.value = wrapTo(value, declaration.type);
			}
			else if (declaration.arraySize != 0)
			{
				variable.isArray = true;
				variable.count = declaration.arraySize;
				variable.lo = newPlainLabel(variableLabel(declaration.name));
			}
			else
			{
				variable.lo = newLabel(variableLabel(declaration.name));
				if (declaration.type.size == 2)
					variable.hi = newLabel(variableLabel(declaration.name) + "_hi");
			}
			scope[declaration.name] = variable;
			return variable;
		}

		// the type a variable has in an expression: an array is the address of its first element
		static Type valueType(const Variable& variable)
		{
			return variable.isArray ? pointerTo(variable.type) : variable.type;
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
			if (expr.kind != Expr::VARIABLE)
				return false;
			const Variable& variable = lookup(expr.name, expr.line);
			return !variable.isConst && !variable.isArray;
		}

		static void checkFits(long value, const Type& type, int line)
		{
			if (!fitsLoosely(value, type))
				throw CompileError(line, "the constant " + decimal(value) + " does not fit in " + typeName(type));
		}

		// writes pseudo lines ("@..."), labels ("name:") and instructions
		void writeLines(ostringstream& out, const vector<string>& lines)
		{
			for (size_t i = 0; i < lines.size(); i++)
			{
				const string& line = lines[i];
				if (line[0] == '@')
					out << "        " << line << "\n";
				else if (line[line.size() - 1] == ':')
					out << line << "\n";
				else
					out << instructionLines(line);
			}
		}

		// start code that gives a variable with its own address its first value
		void initialize(ostringstream& out, const Stmt& declaration, const Variable& variable)
		{
			if (variable.isArray)
			{
				writeLines(out, arrayInitialization(declaration, variable));
				return;
			}
			long value = 0;
			if (declaration.expr && !fold(*declaration.expr, &value))
			{
				vector<string> lines;
				if (variable.type.isPointer && pointerInitialization(*declaration.expr, variable, &lines))
				{
					writeLines(out, lines);
					return;
				}
				throw CompileError(declaration.line, "the initializer of '" + declaration.name + "' is not constant");
			}
			checkFits(value, variable.type, declaration.line);
			value = wrapTo(value, variable.type);
			initializeByte(out, variable.lo, value & 0xff);
			if (variable.type.size == 2)
				initializeByte(out, variable.hi, (value >> 8) & 0xff);
		}

		void initializeByte(ostringstream& out, const string& label, long value)
		{
			if (value == 0)
				out << instructionLines("clr " + label);
			else
				out << instructionLines("mov w, #" + hexText(value)) << instructionLines("mov " + label + ", w");
		}

		// The address an array or variable or element has in a pointer that is
		// initialized: a name, &name or &name[constant]. Only W and acc_hi are used.
		bool pointerInitialization(const Expr& expr, const Variable& pointer, vector<string>* lines)
		{
			const Expr* target = &expr;
			if (expr.kind == Expr::UNARY && expr.name == "&")
				target = expr.left.get();
			else if (!(expr.kind == Expr::VARIABLE && lookup(expr.name, expr.line).isArray))
				return false;
			long offset = 0;
			int elementSize = 1;
			if (target->kind == Expr::INDEX)
			{
				if (!fold(*target->right, &offset) || target->left->kind != Expr::VARIABLE)
					return false;
				const Variable& array = lookup(target->left->name, target->left->line);
				if (!array.isArray)
					return false;
				elementSize = array.type.size;
				target = target->left.get();
			}
			if (target->kind != Expr::VARIABLE)
				return false;
			const Variable& variable = lookup(target->name, target->line);
			if (variable.isConst)
				return false;
			if (offset != 0)
			{
				lines->push_back("mov w, #" + hexText((offset * elementSize) & 0xff));
				lines->push_back("mov " + accHigh() + ", w");
			}
			lines->push_back("mov w, #" + (variable.isArray ? variable.lo : takeAddress(variable)));
			if (offset != 0)
				lines->push_back("add w, " + accHigh());
			lines->push_back("mov " + pointer.lo + ", w");
			return true;
		}

		// The name of the physical address of a variable that is no array. A 16
		// bit variable needs both bytes next to each other then.
		string takeAddress(const Variable& variable)
		{
			addressTaken[variable.lo] = variable.hi;
			return variable.lo + "_addr";
		}

		// the code that writes the elements of an array: FSR walks along it
		vector<string> arrayInitialization(const Stmt& declaration, const Variable& variable)
		{
			int size = variable.type.size;
			vector<int> bytes;
			for (int i = 0; i < variable.count; i++)
			{
				long value = 0;
				if (i < (int) declaration.elements.size())
				{
					const Expr& element = *declaration.elements[i];
					if (!fold(element, &value))
						throw CompileError(element.line, "the initializer of the array '" + declaration.name + "' is not constant");
					checkFits(value, variable.type, element.line);
					value = wrapTo(value, variable.type);
				}
				bytes.push_back((int) (value & 0xff));
				if (size == 2)
					bytes.push_back((int) ((value >> 8) & 0xff));
			}
			vector<string> lines;
			lines.push_back("@indirect");
			lines.push_back("mov w, #" + variable.lo);
			lines.push_back("mov fsr, w");
			for (size_t i = 0; i < bytes.size();)
			{
				size_t run = 0;
				while (i + run < bytes.size() && bytes[i + run] == 0)
					run++;
				if (run >= 8)
				{
					// a loop clears the run; acc_hi counts, nothing else is in use here
					string loop = newJumpLabel();
					lines.push_back("mov w, #" + hexText((long) min(run, (size_t) 255)));
					lines.push_back("mov " + accHigh() + ", w");
					lines.push_back(loop + ":");
					lines.push_back("clr indf");
					lines.push_back("inc fsr");
					lines.push_back("decsz " + accHigh());
					lines.push_back("jmp " + loop);
					i += min(run, (size_t) 255);
					continue;
				}
				if (bytes[i] == 0)
					lines.push_back("clr indf");
				else
				{
					lines.push_back("mov w, #" + hexText(bytes[i]));
					lines.push_back("mov indf, w");
				}
				i++;
				if (i < bytes.size())
					lines.push_back("inc fsr");
			}
			lines.push_back("@done");
			return lines;
		}

		void declareGlobal(const Stmt& declaration)
		{
			Variable variable = declare(declaration);
			if (variable.isConst)
				return;
			if (variable.isArray)
			{
				globalBlocks.push_back(blockOf(variable));
				globalLabels[declaration.name] = variable.lo;
			}
			else
			{
				uniqueLabels.push_back(variable.lo);
				globalLabels[declaration.name] = variable.lo;
				if (variable.type.size == 2)
				{
					uniqueLabels.push_back(variable.hi);
					globalHighLabels[declaration.name] = variable.hi;
				}
			}
			initialize(initialization, declaration, variable);
		}

		static Block blockOf(const Variable& variable)
		{
			Block block;
			block.label = variable.lo;
			block.bytes = variable.count * variable.type.size;
			return block;
		}

		void localDeclaration(const Stmt& stmt)
		{
			Variable variable = declare(stmt);
			if (variable.isConst)
				return;
			if (variable.isArray)
			{
				current->blocks.push_back(blockOf(variable));
				if (stmt.isStatic)
					initialize(current->initialization, stmt, variable);
				else if (!stmt.elements.empty())
					writeLines(current->code, arrayInitialization(stmt, variable));
				return;
			}
			vector<string>& labels = stmt.isStatic ? current->staticLabels : current->localLabels;
			labels.push_back(variable.lo);
			if (variable.type.size == 2)
				labels.push_back(variable.hi);
			if (stmt.isStatic)
				initialize(current->initialization, stmt, variable);
			else if (stmt.expr)
				storeExpression(variable.lo, variable.hi, variable.type, *stmt.expr);
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
				if (used == ACC_HIGH)
					usesHighByte = true;
				if (used == ACC_LOW)
					usesLowByte = true;
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

		// Computes a constant expression exactly.
		bool fold(const Expr& expr, long* value) const
		{
			switch (expr.kind)
			{
			case Expr::NUMBER:
				*value = expr.value;
				return true;
			case Expr::VARIABLE:
			{
				const Variable& variable = lookup(expr.name, expr.line);
				*value = variable.value;
				return variable.isConst;
			}
			case Expr::CALL:
			case Expr::INDEX:
			case Expr::INDIRECT:
				return false;
			case Expr::CAST:
			{
				long operand;
				if (!fold(*expr.left, &operand))
					return false;
				*value = wrapTo(operand, expr.type);
				return true;
			}
			case Expr::UNARY:
			{
				if (expr.name == "*" || expr.name == "&")
					return false;
				long operand;
				if (!fold(*expr.left, &operand))
					return false;
				if (expr.name == "-") *value = -operand;
				else if (expr.name == "~") *value = ~operand;
				else *value = operand == 0;
				return true;
			}
			default:
			{
				long a, b;
				bool left = fold(*expr.left, &a);
				bool right = fold(*expr.right, &b);
				if (!left || !right)
					return false;
				*value = apply(expr.name, a, b, expr.line);
				return true;
			}
			}
		}

		static long apply(const string& op, long a, long b, int line)
		{
			if (op == "+") return a + b;
			if (op == "-") return a - b;
			if (op == "&") return a & b;
			if (op == "|") return a | b;
			if (op == "^") return a ^ b;
			if (op == "<<" || op == ">>")
			{
				if (b < 0 || b > 31)
					throw CompileError(line, "the shift count " + decimal(b) + " is out of range");
				return op == "<<" ? a << b : a >> b;
			}
			if (op == "==") return a == b;
			if (op == "!=") return a != b;
			if (op == "<") return a < b;
			if (op == ">") return a > b;
			if (op == "<=") return a <= b;
			if (op == ">=") return a >= b;
			if (op == "&&") return a && b;
			return a || b;
		}

		bool isConstant(const Expr& expr) const
		{
			long value;
			return fold(expr, &value);
		}

		bool isSimple(const Expr& expr) const
		{
			return isPlainVariable(expr) || isConstant(expr);
		}

		// types

		Type typeOf(const Expr& expr)
		{
			if (expr.kind == Expr::CAST)
				return expr.type;
			long value;
			if (fold(expr, &value))
				return constantType(value, expr.line);
			switch (expr.kind)
			{
			case Expr::VARIABLE:
				return valueType(lookup(expr.name, expr.line));
			case Expr::CALL:
			{
				FunctionInfo& callee = lookupFunction(expr.name, expr.line);
				if (!callee.returnsValue)
					throw CompileError(expr.line, "function '" + callee.name + "' returns no value");
				return callee.returnType;
			}
			case Expr::INDIRECT:
				return expr.type;
			case Expr::INDEX:
				return pointee(typeOf(*expr.left), expr.line, "only arrays and pointers can be indexed");
			case Expr::UNARY:
				if (expr.name == "!")
					return U8;
				if (expr.name == "*")
					return pointee(typeOf(*expr.left), expr.line, "cannot dereference a value that is no pointer");
				if (expr.name == "&")
					return addressType(*expr.left, expr.line);
				return noPointer(typeOf(*expr.left), expr.name, expr.line);
			default:
				if (isComparison(expr.name) || isLogical(expr.name))
					return U8;
				if (expr.name == "<<" || expr.name == ">>")
					return noPointer(typeOf(*expr.left), expr.name, expr.line);
				{
					Type a = typeOf(*expr.left);
					Type b = typeOf(*expr.right);
					if (a.isPointer || b.isPointer)
						return pointerResult(expr.name, a, b, expr.line);
				}
				return operationType(*expr.left, *expr.right, false);
			}
		}

		static Type pointee(const Type& type, int line, const string& message)
		{
			if (!type.isPointer)
				throw CompileError(line, message + " (it is " + typeName(type) + ")");
			return type.pointee();
		}

		static Type noPointer(const Type& type, const string& op, int line)
		{
			if (type.isPointer)
				throw CompileError(line, "the operator '" + op + "' cannot be used on a pointer");
			return type;
		}

		// the type of an operation with a pointer: pointer + number, pointer - number
		static Type pointerResult(const string& op, const Type& a, const Type& b, int line)
		{
			if (op == "+" && a.isPointer != b.isPointer)
				return a.isPointer ? a : b;
			if (op == "-" && a.isPointer && !b.isPointer)
				return a;
			if (a.isPointer && b.isPointer && (op == "+" || op == "-"))
				throw CompileError(line, op == "+" ? "two pointers cannot be added" : "the difference of two pointers is not supported yet");
			throw CompileError(line, "the operator '" + op + "' cannot be used on a pointer");
		}

		// the type of &operand: a pointer to a variable, an element or what a pointer points to
		Type addressType(const Expr& operand, int line)
		{
			switch (operand.kind)
			{
			case Expr::VARIABLE:
			{
				const Variable& variable = lookup(operand.name, operand.line);
				if (variable.isConst)
					throw CompileError(line, "cannot take the address of the const '" + operand.name + "'");
				return pointerTo(variable.type);
			}
			case Expr::INDEX:
				return pointerTo(typeOf(operand));
			case Expr::UNARY:
				if (operand.name == "*")
				{
					Type pointer = typeOf(*operand.left);
					pointee(pointer, line, "cannot dereference a value that is no pointer");
					return pointer;
				}
				break;
			default:
				break;
			}
			throw CompileError(line, "cannot take the address of this expression");
		}

		// A constant without a type of its own: it takes the type of the other
		// operand. A cast gives a constant its type.
		bool isUntypedConstant(const Expr& expr, long* value) const
		{
			return expr.kind != Expr::CAST && fold(expr, value);
		}

		// The type an operation on a and b is done in. A constant takes the type of
		// the other operand if it fits; arithmetic is modular, so it may be a
		// negative constant for an unsigned type. A comparison is exact: the
		// operation is widened until it holds both. Addresses compare as numbers.
		Type operationType(const Expr& a, const Expr& b, bool isComparisonOperation)
		{
			long constantA, constantB;
			bool aIsConstant = isUntypedConstant(a, &constantA);
			bool bIsConstant = isUntypedConstant(b, &constantB);
			if (aIsConstant && bIsConstant)
				return commonType(constantType(constantA, a.line), constantType(constantB, b.line));
			Type typeA = aIsConstant ? U8 : typeOf(a);
			Type typeB = bIsConstant ? U8 : typeOf(b);
			if (typeA.isPointer || typeB.isPointer)
				return U8;
			if (aIsConstant)
				return withConstant(typeB, constantA, a.line, isComparisonOperation);
			if (bIsConstant)
				return withConstant(typeA, constantB, b.line, isComparisonOperation);
			return commonType(typeA, typeB);
		}

		static Type withConstant(const Type& type, long constant, int line, bool isComparisonOperation)
		{
			if (isComparisonOperation)
				return fitsExactly(constant, type) ? type : holdingBoth(type, constant);
			if (fitsLoosely(constant, type))
				return type;
			return commonType(type, constantType(constant, line));
		}

		// 16 bit values

		string accHigh()
		{
			usesHighByte = true;
			return ACC_HIGH;
		}

		string accLow()
		{
			usesLowByte = true;
			return ACC_LOW;
		}

		// W (and acc_hi) = the constant, of the size of the type
		void loadConstant(long value, const Type& type)
		{
			value = wrapTo(value, type);
			if (type.size == 2)
			{
				long high = (value >> 8) & 0xff;
				if (high == 0)
					emit("clr " + accHigh());
				else
				{
					emit("mov w, #" + hexText(high));
					emit("mov " + accHigh() + ", w");
				}
			}
			long low = value & 0xff;
			if (low == 0)
				emit("clr w");
			else
				emit("mov w, #" + hexText(low));
		}

		void loadVariable(const Variable& variable)
		{
			if (variable.type.size == 2)
			{
				emit("mov w, " + variable.hi);
				emit("mov " + accHigh() + ", w");
			}
			emit("mov w, " + variable.lo);
		}

		// the value in W (and acc_hi) into the registers; W is not kept
		void storeAccumulator(const string& lo, const string& hi, const Type& type)
		{
			emit("mov " + lo + ", w");
			if (type.size == 2)
			{
				emit("mov w, " + accHigh());
				emit("mov " + hi + ", w");
			}
		}

		// the value in W (and acc_hi) into new temporaries
		Operand spill(const Type& type, int line)
		{
			Operand operand;
			int low = allocateTemporary(line);
			operand.lo = temporary(low);
			operand.temporaries = 1;
			if (type.size == 2)
			{
				operand.hi = temporary(allocateTemporary(line));
				operand.temporaries = 2;
			}
			storeAccumulator(operand.lo, operand.hi, type);
			return operand;
		}

		void release(const Operand& operand)
		{
			for (int i = 0; i < operand.temporaries; i++)
				freeTemporary();
		}

		// the value in W (and acc_hi) changes its type
		void convert(const Type& from, const Type& to, int line)
		{
			if (from.size == to.size || to.size == 1)
				return;
			if (!from.isSigned)
			{
				emit("clr " + accHigh());
				return;
			}
			// sign extension: the high byte is 0xFF if bit 7 of the low byte is set
			int t = allocateTemporary(line);
			emit("mov " + temporary(t) + ", w");
			emit("clr " + accHigh());
			emit("snb " + temporary(t) + ".7");
			emit("dec " + accHigh());
			emit("mov w, " + temporary(t));
			freeTemporary();
		}

		// W (and acc_hi) = the expression in the type
		void expressionAs(const Expr& expr, const Type& type)
		{
			long value;
			if (fold(expr, &value))
			{
				checkFits(value, type, expr.line);
				loadConstant(value, type);
				return;
			}
			Type planned = typeOf(expr);
			Type produced = expression(expr);
			if (produced != planned)
				throw CompileError(expr.line, "internal error: the type of an expression is " + typeName(produced) + ", not " + typeName(planned));
			if (produced.isPointer && type.isPointer && produced.pointeeSize != type.pointeeSize)
				throw CompileError(expr.line, "pointers to " + typeName(produced.pointee()) + " and " + typeName(type.pointee()) +
					" are not compatible, use a cast");
			convert(produced, type, expr.line);
		}

		// where a value of the type is without code: a variable of the same size
		bool isInPlace(const Expr& expr, const Type& type) const
		{
			return isPlainVariable(expr) && lookup(expr.name, expr.line).type.size == type.size;
		}

		// A value in registers: a variable of the right size, or the expression in a temporary.
		Operand locate(const Expr& expr, const Type& type)
		{
			if (isInPlace(expr, type))
			{
				Operand operand;
				const Variable& variable = lookup(expr.name, expr.line);
				operand.lo = variable.lo;
				operand.hi = variable.hi;
				return operand;
			}
			expressionAs(expr, type);
			return spill(type, expr.line);
		}

		// expressions

		// W (and acc_hi) = the expression, in its own type, which is returned
		Type expression(const Expr& expr)
		{
			if (expr.kind == Expr::CAST)
			{
				Type from = expression(*expr.left);
				convert(from, expr.type, expr.line);
				return expr.type;
			}
			long value;
			if (fold(expr, &value))
			{
				Type type = constantType(value, expr.line);
				loadConstant(value, type);
				return type;
			}
			switch (expr.kind)
			{
			case Expr::VARIABLE:
			{
				const Variable& variable = lookup(expr.name, expr.line);
				if (variable.isArray)
				{
					emit("mov w, #" + variable.lo);		// the address of the first element
					return valueType(variable);
				}
				loadVariable(variable);
				return variable.type;
			}
			case Expr::CALL:
				return call(expr, true);
			case Expr::INDIRECT:
				emit("mov w, " + expr.name);
				loadThrough(expr.type);
				return expr.type;
			case Expr::INDEX:
			{
				Type element = typeOf(expr);
				addressOfElement(*expr.left, *expr.right, expr.line);
				loadThrough(element);
				return element;
			}
			case Expr::UNARY:
				if (expr.name == "*")
				{
					Type element = typeOf(expr);
					expressionAs(*expr.left, typeOf(*expr.left));
					loadThrough(element);
					return element;
				}
				if (expr.name == "&")
					return addressOf(expr);
				return unaryExpression(expr);
			default:
				return binaryExpression(expr);
			}
		}

		// pointers

		void marker(const string& text)
		{
			current->code << "        " << text << "\n";
		}

		// W = the value at the address in W (and acc_hi for 16 bits). Only direct
		// registers are used while FSR points there: a bank instruction would
		// change FSR.
		void loadThrough(const Type& element)
		{
			marker("@indirect");
			emit("mov fsr, w");
			emit("mov w, indf");
			if (element.size == 2)
			{
				emit("mov " + accLow() + ", w");
				emit("inc fsr");
				emit("mov w, indf");
				emit("mov " + accHigh() + ", w");
				emit("mov w, " + accLow());
			}
			marker("@done");
		}

		// The value in W (and acc_hi) goes to the address in the temporary. FSR
		// is set last, with only direct registers left.
		void storeThrough(const string& addressTemporary, const Type& element)
		{
			emit("mov " + accLow() + ", w");
			emit("mov w, " + addressTemporary);
			marker("@indirect");
			emit("mov fsr, w");
			emit("mov w, " + accLow());
			emit("mov indf, w");
			if (element.size == 2)
			{
				emit("inc fsr");
				emit("mov w, " + accHigh());
				emit("mov indf, w");
			}
			marker("@done");
		}

		// W = the offset of an element: the index, times 2 for 16 bit elements. An
		// address has 8 bits, only the low byte of the index counts.
		void scaledOffset(const Expr& index, int elementSize, int line)
		{
			expressionAs(index, U8);
			if (elementSize == 2)
			{
				int t = allocateTemporary(line);
				emit("mov " + temporary(t) + ", w");
				emit("clrb status.0");
				emit("rl " + temporary(t));
				emit("mov w, " + temporary(t));
				freeTemporary();
			}
		}

		// W = the address of base[index]
		void addressOfElement(const Expr& base, const Expr& index, int line)
		{
			Type baseType = typeOf(base);
			pointee(baseType, line, "only arrays and pointers can be indexed");
			long constantIndex;
			if (fold(index, &constantIndex) && constantIndex == 0)
			{
				expressionAs(base, baseType);
				return;
			}
			scaledOffset(index, baseType.pointeeSize, line);
			int t = allocateTemporary(line);
			emit("mov " + temporary(t) + ", w");
			expressionAs(base, baseType);
			emit("add w, " + temporary(t));
			freeTemporary();
		}

		// W = the address of what *p or p[i] names
		void lvalueAddress(const Expr& target)
		{
			if (target.kind == Expr::INDEX)
				addressOfElement(*target.left, *target.right, target.line);
			else
				expressionAs(*target.left, typeOf(*target.left));
		}

		// W = &operand
		Type addressOf(const Expr& expr)
		{
			Type result = typeOf(expr);
			const Expr& operand = *expr.left;
			if (operand.kind == Expr::VARIABLE)
			{
				const Variable& variable = lookup(operand.name, operand.line);
				emit("mov w, #" + (variable.isArray ? variable.lo : takeAddress(variable)));
			}
			else
				lvalueAddress(operand);
			return result;
		}

		// W = pointer + number or pointer - number, the number counts elements
		Type pointerArithmetic(const string& op, const Expr& a, const Expr& b, int line)
		{
			Type typeA = typeOf(a);
			Type typeB = typeOf(b);
			Type result = pointerResult(op, typeA, typeB, line);
			const Expr& pointer = typeA.isPointer ? a : b;
			const Expr& offset = typeA.isPointer ? b : a;
			int t = allocateTemporary(line);
			if (op == "-")
			{
				expressionAs(pointer, result);
				emit("mov " + temporary(t) + ", w");
				scaledOffset(offset, result.pointeeSize, line);
				emit("mov w, " + temporary(t) + "-w");
			}
			else
			{
				scaledOffset(offset, result.pointeeSize, line);
				emit("mov " + temporary(t) + ", w");
				expressionAs(pointer, result);
				emit("add w, " + temporary(t));
			}
			freeTemporary();
			return result;
		}

		// W = ~value. "xor w, #ff" cannot be used: its opcode 0xFFF is what the
		// emulator takes for an empty ROM word.
		Type complement(const Expr& value, int line)
		{
			Type type = typeOf(value);
			if (type.size == 1 && isPlainVariable(value))
			{
				emit("mov w, /" + lookup(value.name, value.line).lo);
				return type;
			}
			expressionAs(value, type);
			int t = allocateTemporary(line);
			emit("mov " + temporary(t) + ", w");
			emit("mov w, /" + temporary(t));
			if (type.size == 2)
			{
				emit("mov " + temporary(t) + ", w");
				emit("mov w, /" + accHigh());
				emit("mov " + accHigh() + ", w");
				emit("mov w, " + temporary(t));
			}
			freeTemporary();
			return type;
		}

		Type unaryExpression(const Expr& expr)
		{
			if (expr.name == "!")
			{
				booleanValue(expr);
				return U8;
			}
			noPointer(typeOf(*expr.left), expr.name, expr.line);
			Type type = complement(*expr.left, expr.line);
			if (expr.name == "-")
			{
				// -x is ~x + 1
				int t = allocateTemporary(expr.line);
				emit("mov " + temporary(t) + ", w");
				emit("mov w, #1");
				emit("add w, " + temporary(t));
				if (type.size == 2)
				{
					emit("snb status.0");
					emit("inc " + accHigh());
				}
				freeTemporary();
			}
			return type;
		}

		Type binaryExpression(const Expr& expr)
		{
			const string& op = expr.name;
			if (isComparison(op) || isLogical(op))
			{
				booleanValue(expr);
				return U8;
			}
			return arithmetic(op, *expr.left, *expr.right, expr.line);
		}

		// W (and acc_hi) = a op b for + - & | ^ << >>
		Type arithmetic(const string& op, const Expr& a, const Expr& b, int line)
		{
			if (op == "<<" || op == ">>")
				return shift(op, a, b, line);
			if (typeOf(a).isPointer || typeOf(b).isPointer)
				return pointerArithmetic(op, a, b, line);
			Type type = operationType(a, b, false);
			if (type.size == 2)
			{
				wideOperation(op, a, b, type);
				return type;
			}
			if (op == "-")
				subtract(a, b, line);
			else
				combine(op, a, b, line);
			return type;
		}

		// 8 bit: W = a op b for + & | ^
		void combine(const string& op, const Expr& a, const Expr& b, int line)
		{
			if (!isSimple(b) && isSimple(a))
			{
				combine(op, b, a, line);		// all four are commutative
				return;
			}
			if (isSimple(b))
			{
				long k;
				if (fold(b, &k))
					combineWithConstant(op, a, (int) (k & 0xff), line);
				else
				{
					expressionAs(a, U8);
					emit(mnemonic(op) + " w, " + lookup(b.name, b.line).lo);
				}
				return;
			}
			expressionAs(b, U8);
			int t = allocateTemporary(line);
			emit("mov " + temporary(t) + ", w");
			expressionAs(a, U8);
			emit(mnemonic(op) + " w, " + temporary(t));
			freeTemporary();
		}

		void combineWithConstant(const string& op, const Expr& a, int k, int line)
		{
			if (op == "+")
			{
				if (k == 0)
					expressionAs(a, U8);
				else if (isPlainVariable(a))
				{
					emit("mov w, #" + hexText(k));
					emit("add w, " + lookup(a.name, a.line).lo);
				}
				else
				{
					expressionAs(a, U8);
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
			expressionAs(a, U8);
			if (op == "&" && k == 0)
				emit("clr w");		// "and w, #0" would and with FSR
			else
				emit(mnemonic(op) + " w, #" + hexText(k));
		}

		// 8 bit: W = a - b; the carry is set if a >= b (unsigned)
		void subtract(const Expr& a, const Expr& b, int line)
		{
			string from;
			bool temporaryUsed = false;
			if (isPlainVariable(a) && lookup(a.name, a.line).type.size == 1)
				from = lookup(a.name, a.line).lo;
			else
			{
				expressionAs(a, U8);
				int t = allocateTemporary(line);
				emit("mov " + temporary(t) + ", w");
				from = temporary(t);
				temporaryUsed = true;
			}
			expressionAs(b, U8);
			emit("mov w, " + from + "-w");
			if (temporaryUsed)
				freeTemporary();
		}

		// 8 bit signed: W = a - b after turning the sign bits; the carry is set if a >= b
		void subtractSigned(const Expr& a, const Expr& b, int line)
		{
			expressionAs(a, I8);
			emit("xor w, #80");
			int t = allocateTemporary(line);
			emit("mov " + temporary(t) + ", w");
			expressionAs(b, I8);
			emit("xor w, #80");
			emit("mov w, " + temporary(t) + "-w");
			freeTemporary();
		}

		// 16 bit: W and acc_hi = a op b for + - & | ^, all from registers
		void wideOperation(const string& op, const Expr& a, const Expr& b, const Type& type)
		{
			Operand left = locate(a, type);
			Operand right = locate(b, type);
			if (op == "-")
			{
				emit("mov w, " + right.hi);
				emit("mov w, " + left.hi + "-w");
				emit("mov " + accHigh() + ", w");
				emit("mov w, " + right.lo);
				emit("mov w, " + left.lo + "-w");
				emit("sb status.0");		// a borrow takes one from the high byte
				emit("dec " + accHigh());
			}
			else
			{
				emit("mov w, " + left.hi);
				emit(mnemonic(op) + " w, " + right.hi);
				emit("mov " + accHigh() + ", w");
				emit("mov w, " + left.lo);
				emit(mnemonic(op) + " w, " + right.lo);
				if (op == "+")
				{
					emit("snb status.0");		// a carry adds one to the high byte
					emit("inc " + accHigh());
				}
			}
			release(right);
			release(left);
		}

		// W (and acc_hi) = value << count or value >> count, the count is constant
		Type shift(const string& op, const Expr& valueExpr, const Expr& countExpr, int line)
		{
			long count;
			if (!fold(countExpr, &count))
				throw CompileError(line, "the shift count must be constant");
			if (count < 0)
				throw CompileError(line, "the shift count " + decimal(count) + " is negative");
			Type type = noPointer(typeOf(valueExpr), op, line);
			expressionAs(valueExpr, type);
			if (count == 0)
				return type;
			bool arithmeticRight = op == ">>" && type.isSigned;
			int bits = type.size * 8;
			if (count >= bits)
			{
				if (!arithmeticRight)
				{
					emit("clr w");
					if (type.size == 2)
						emit("clr " + accHigh());
					return type;
				}
				count = bits - 1;		// the sign fills everything
			}
			int t = allocateTemporary(line);
			emit("mov " + temporary(t) + ", w");
			string top = type.size == 2 ? accHigh() : temporary(t);
			for (long i = 0; i < count; i++)
			{
				emit("clrb status.0");
				if (arithmeticRight)
				{
					emit("snb " + top + ".7");
					emit("setb status.0");
				}
				if (op == "<<")
				{
					emit("rl " + temporary(t));
					if (type.size == 2)
						emit("rl " + accHigh());
				}
				else
				{
					if (type.size == 2)
						emit("rr " + accHigh());
					emit("rr " + temporary(t));
				}
			}
			emit("mov w, " + temporary(t));
			freeTemporary();
			return type;
		}

		// calls

		// W (and acc_hi) = the result of the function
		Type call(const Expr& expr, bool valueNeeded)
		{
			if (expr.name == "main")
				throw CompileError(expr.line, "main cannot be called");
			FunctionInfo& callee = lookupFunction(expr.name, expr.line);
			if (expr.arguments.size() != callee.parameters.size())
				throw CompileError(expr.line, "function '" + callee.name + "' takes " + decimal((long) callee.parameters.size()) +
					" argument(s), not " + decimal((long) expr.arguments.size()));
			if (valueNeeded && !callee.returnsValue)
				throw CompileError(expr.line, "function '" + callee.name + "' returns no value");
			if (!current->calls.count(callee.name))
				current->calls[callee.name] = expr.line;
			ensureParameterLabels(callee);

			// An argument that contains a call may run this very function and
			// write its parameters: the arguments before it wait in temporaries.
			size_t count = expr.arguments.size();
			vector<Operand> held(count);
			for (size_t i = 0; i < count; i++)
			{
				const Type& type = callee.parameters[i].type;
				bool laterCall = false;
				for (size_t j = i + 1; j < count; j++)
					laterCall = laterCall || hasCall(*expr.arguments[j]);
				if (laterCall)
				{
					expressionAs(*expr.arguments[i], type);
					held[i] = spill(type, expr.line);
				}
				else
					storeExpression(callee.parameterLabels[i].lo, callee.parameterLabels[i].hi, type, *expr.arguments[i]);
			}
			for (size_t i = 0; i < count; i++)
			{
				if (held[i].temporaries == 0)
					continue;
				const Type& type = callee.parameters[i].type;
				if (type.size == 2)
				{
					emit("mov w, " + held[i].hi);
					emit("mov " + callee.parameterLabels[i].hi + ", w");
				}
				emit("mov w, " + held[i].lo);
				emit("mov " + callee.parameterLabels[i].lo + ", w");
			}
			for (size_t i = count; i > 0; i--)
				release(held[i - 1]);
			emit("call " + callee.entry);
			return callee.returnType;
		}

		// The parameters of a function have names before the function is made.
		void ensureParameterLabels(FunctionInfo& function)
		{
			if (function.parameterLabels.size() == function.parameters.size())
				return;
			function.parameterLabels.clear();
			for (size_t i = 0; i < function.parameters.size(); i++)
				function.parameterLabels.push_back(parameterLabel(function, function.parameters[i]));
		}

		ParameterLabels parameterLabel(const FunctionInfo& function, const Parameter& parameter)
		{
			string wanted = "v_" + function.name + "_" + parameter.name;
			map<string, ParameterLabels>::const_iterator found = parameterNames.find(wanted);
			if (found != parameterNames.end())
				return found->second;
			ParameterLabels labels;
			labels.lo = newLabel(wanted);
			if (parameter.type.size == 2)
				labels.hi = newLabel(wanted + "_hi");
			parameterNames[wanted] = labels;
			return labels;
		}

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
			long value;
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
			if (expr.kind == Expr::BINARY && isLogical(expr.name))
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
			Type type = expression(expr);
			if (type.size == 2)
				emit("or w, " + accHigh());		// the zero flag follows both bytes
			else
				emit("or w, #0");		// the zero flag follows W
			// Z is set if the value is 0, i.e. false
			emit(onTrue ? "sb status.2" : "snb status.2");
			emit("jmp " + target);
		}

		// 16 bit: Z is set if a == b
		void wideEqual(const Expr& a, const Expr& b, const Type& type, int line)
		{
			Operand left = locate(a, type);
			Operand right = locate(b, type);
			int t = allocateTemporary(line);
			emit("mov w, " + left.hi);
			emit("xor w, " + right.hi);
			emit("mov " + temporary(t) + ", w");
			emit("mov w, " + left.lo);
			emit("xor w, " + right.lo);
			emit("or w, " + temporary(t));
			freeTemporary();
			release(right);
			release(left);
		}

		// 16 bit: the carry is set if a >= b
		void wideCompare(const Expr& a, const Expr& b, const Type& type, int line)
		{
			Operand left = locate(a, type);
			Operand right = locate(b, type);
			string done = newJumpLabel();
			if (type.isSigned)
			{
				// turn the sign bits, then compare as unsigned
				int t = allocateTemporary(line);
				emit("mov w, " + left.hi);
				emit("xor w, #80");
				emit("mov " + temporary(t) + ", w");
				emit("mov w, " + right.hi);
				emit("xor w, #80");
				emit("mov w, " + temporary(t) + "-w");
				freeTemporary();
			}
			else
			{
				emit("mov w, " + right.hi);
				emit("mov w, " + left.hi + "-w");
			}
			emit("sb status.2");		// equal high bytes: the low bytes decide
			emit("jmp " + done);
			emit("mov w, " + right.lo);
			emit("mov w, " + left.lo + "-w");
			label(done);
			release(right);
			release(left);
		}

		void compareAndJump(bool onTrue, const Expr& expr, const string& target)
		{
			const string& op = expr.name;
			Type type = operationType(*expr.left, *expr.right, true);
			if (op == "==" || op == "!=")
			{
				if (type.size == 2)
					wideEqual(*expr.left, *expr.right, type, expr.line);
				else
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
			// the carry is set if a >= b
			if (type.size == 2)
				wideCompare(a, b, type, expr.line);
			else if (type.isSigned)
				subtractSigned(a, b, expr.line);
			else
				subtract(a, b, expr.line);
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
				variable.type = parameter.type;
				variable.lo = function.parameterLabels[i].lo;
				variable.hi = function.parameterLabels[i].hi;
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
				if (stmt.target)
					assignThrough(stmt);
				else
					assignment(stmt);
				break;
			case Stmt::STEP:
				if (stmt.target)
					stepThrough(stmt);
				else
					step(stmt);
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

		// the registers = the value, in the type
		void storeExpression(const string& lo, const string& hi, const Type& type, const Expr& value)
		{
			long constant;
			if (fold(value, &constant))
			{
				checkFits(constant, type, value.line);
				constant = wrapTo(constant, type);
				storeConstantByte(lo, constant & 0xff);
				if (type.size == 2)
					storeConstantByte(hi, (constant >> 8) & 0xff);
				return;
			}
			expressionAs(value, type);
			storeAccumulator(lo, hi, type);
		}

		void storeConstantByte(const string& label, long value)
		{
			if (value == 0)
			{
				emit("clr " + label);
				return;
			}
			emit("mov w, #" + hexText(value));
			emit("mov " + label + ", w");
		}

		void assignment(const Stmt& stmt)
		{
			const Variable& variable = lookupForWriting(stmt.name, stmt.line);
			if (variable.isArray)
				throw CompileError(stmt.line, "the array '" + stmt.name + "' cannot be assigned to");
			if (stmt.op == "=")
			{
				storeExpression(variable.lo, variable.hi, variable.type, *stmt.expr);
				return;
			}
			// a op= b is a = a op b
			Expr self(Expr::VARIABLE, stmt.line);
			self.name = stmt.name;
			string op = stmt.op.substr(0, stmt.op.size() - 1);
			Type type = arithmetic(op, self, *stmt.expr, stmt.line);
			convert(type, variable.type, stmt.line);
			storeAccumulator(variable.lo, variable.hi, variable.type);
		}

		void step(const Stmt& stmt)
		{
			const Variable& variable = lookupForWriting(stmt.name, stmt.line);
			if (variable.isArray)
				throw CompileError(stmt.line, "the array '" + stmt.name + "' cannot be changed");
			bool increment = stmt.op == "++";
			if (variable.type.size == 1)
			{
				int times = variable.type.isPointer ? variable.type.pointeeSize : 1;
				for (int i = 0; i < times; i++)
					emit(string(increment ? "inc " : "dec ") + variable.lo);
				return;
			}
			if (increment)
			{
				emit("inc " + variable.lo);
				emit("snb status.2");		// the low byte wrapped around to 0
				emit("inc " + variable.hi);
			}
			else
			{
				emit("mov w, #1");
				emit("sub " + variable.lo + ", w");
				emit("sb status.0");		// a borrow
				emit("dec " + variable.hi);
			}
		}

		// *p = value, a[i] = value, and with an operator: a[i] += value
		void assignThrough(const Stmt& stmt)
		{
			Type element = typeOf(*stmt.target);
			string op = stmt.op.substr(0, stmt.op.size() - 1);
			changeThrough(*stmt.target, element, op, *stmt.expr, stmt.line);
		}

		// *p++ and a[i]-- change the element by one
		void stepThrough(const Stmt& stmt)
		{
			Type element = typeOf(*stmt.target);
			Expr one(Expr::NUMBER, stmt.line);
			one.value = 1;
			changeThrough(*stmt.target, element, stmt.op == "++" ? "+" : "-", one, stmt.line);
		}

		// the element = value (op empty), or the element = element op value
		void changeThrough(const Expr& target, const Type& element, const string& op, const Expr& value, int line)
		{
			long constant;
			if (op.empty() && fold(value, &constant))
			{
				// FSR first, then the constant goes straight in
				checkFits(constant, element, value.line);
				constant = wrapTo(constant, element);
				lvalueAddress(target);
				marker("@indirect");
				emit("mov fsr, w");
				writeConstantThrough(constant & 0xff);
				if (element.size == 2)
				{
					emit("inc fsr");
					writeConstantThrough((constant >> 8) & 0xff);
				}
				marker("@done");
				return;
			}
			lvalueAddress(target);
			Operand address = spill(U8, line);
			if (op.empty())
				expressionAs(value, element);
			else
			{
				Expr loaded(Expr::INDIRECT, line);
				loaded.name = address.lo;
				loaded.type = element;
				Type type = arithmetic(op, loaded, value, line);
				convert(type, element, line);
			}
			storeThrough(address.lo, element);
			release(address);
		}

		void writeConstantThrough(long byte)
		{
			if (byte == 0)
			{
				emit("clr indf");
				return;
			}
			emit("mov w, #" + hexText(byte));
			emit("mov indf, w");
		}

		void returnStatement(const Stmt& stmt)
		{
			if (current->returnsValue && !stmt.expr)
				throw CompileError(stmt.line, "'return' needs a value in function '" + current->name + "'");
			if (!current->returnsValue && stmt.expr)
				throw CompileError(stmt.line, "'return' with a value in the void function '" + current->name + "'");
			if (stmt.expr)
				expressionAs(*stmt.expr, current->returnType);
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
			for (string name = "main"; deepest.count(name); name = deepest[name])
				chain += " -> " + deepest[name];
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
			vector<Block> blocks = globalBlocks;
			for (size_t i = 0; i < reachable.size(); i++)
			{
				const FunctionInfo& function = functions[reachable[i]];
				for (size_t p = 0; p < function.parameterLabels.size(); p++)
				{
					places.push_back(vector<string>(1, function.parameterLabels[p].lo));
					if (!function.parameterLabels[p].hi.empty())
						places.push_back(vector<string>(1, function.parameterLabels[p].hi));
				}
				for (size_t p = 0; p < function.staticLabels.size(); p++)
					places.push_back(vector<string>(1, function.staticLabels[p]));
				blocks.insert(blocks.end(), function.blocks.begin(), function.blocks.end());
			}
			if (usesHighByte)
			{
				places.push_back(vector<string>(1, ACC_HIGH));
				weights[ACC_HIGH] += 1 << 20;		// always in a direct register: no bank between a skip and its instruction
			}
			if (usesLowByte)
			{
				places.push_back(vector<string>(1, ACC_LOW));
				weights[ACC_LOW] += 1 << 20;
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

			// 16 bit variables whose address is taken need their two bytes next to each other
			set<string> moved;
			for (map<string, string>::const_iterator taken = addressTaken.begin(); taken != addressTaken.end(); ++taken)
			{
				if (taken->second.empty() || !isPlaced(places, taken->first))
					continue;
				Block block;
				block.label = taken->first;
				block.high = taken->second;
				block.bytes = 2;
				blocks.push_back(block);
				moved.insert(taken->first);
				moved.insert(taken->second);
			}
			vector<vector<string> > kept;
			for (size_t p = 0; p < places.size(); p++)
			{
				vector<string> labels;
				for (size_t l = 0; l < places[p].size(); l++)
				{
					if (!moved.count(places[p][l]))
						labels.push_back(places[p][l]);
				}
				if (!labels.empty())
					kept.push_back(labels);
			}
			places = kept;

			int blockBytes = 0;
			for (size_t i = 0; i < blocks.size(); i++)
				blockBytes += blocks[i].bytes;
			int available = (DIRECT_LAST - DIRECT_FIRST + 1) + (BANKED_LAST - BANKED_FIRST + 1);
			int needed = (int) places.size() + blockBytes;
			if (needed > available || blockBytes > BANKED_LAST - BANKED_FIRST + 1)
				throw CompileError(mainFunction->line, "out of RAM: " + decimal(needed) + " bytes needed, " + decimal(available) +
					" available (" + decimal((long) places.size()) + " for variables, parameters and temporaries, " +
					decimal(blockBytes) + " for arrays; arrays have to fit in the banks, which hold " +
					decimal(BANKED_LAST - BANKED_FIRST + 1) + ")");

			// the blocks sit at the end of the banks, one run each
			int cursor = BANKED_LAST + 1;
			for (size_t i = 0; i < blocks.size(); i++)
			{
				cursor -= blocks[i].bytes;
				(*addresses)[blocks[i].label] = cursor;
				if (!blocks[i].high.empty())
					(*addresses)[blocks[i].high] = cursor + 1;
				if (blocks[i].high.empty())
					arrayLabels.insert(blocks[i].label);
			}

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

		static bool isPlaced(const vector<vector<string> >& places, const string& label)
		{
			for (size_t p = 0; p < places.size(); p++)
			{
				if (find(places[p].begin(), places[p].end(), label) != places[p].end())
					return true;
			}
			return false;
		}

		// The register an instruction names for the RAM address: banked bytes
		// are 0x10-0x1F of the selected bank.
		static int registerOf(int address)
		{
			return address < BANKED_FIRST ? address : BANKED_FIRST | (address & 0xf);
		}

		// Replaces "@bank name" by "bank n" where the bank is not selected yet.
		// After reset FSR selects bank 0. The bank is not known after a label
		// (the code can be reached from elsewhere) and after a call. A "bank"
		// between a skip and the instruction it skips is skipped with it; the
		// bank is unknown after that.
		static string resolveBanks(const string& text, const map<string, int>& addresses)
		{
			istringstream in(text);
			ostringstream out;
			string line;
			string previous;
			int bank = -1;
			bool unknownAfterNext = false;
			bool indirect = false;		// FSR points to the data: no bank instruction may change it
			while (getline(in, line))
			{
				size_t first = line.find_first_not_of(' ');
				string trimmed = first == string::npos ? "" : line.substr(first);
				if (trimmed == "@indirect" || trimmed == "@done")
				{
					indirect = trimmed == "@indirect";
					bank = -1;		// FSR is a pointer now or was one
					continue;
				}
				if (trimmed.compare(0, 6, "@bank ") == 0)
				{
					int address = addresses.at(trimmed.substr(6));
					if (address >= BANKED_FIRST && bank != address / 16)
					{
						if (indirect)
							throw CompileError(0, "internal error: " + trimmed.substr(6) + " is used while FSR points to data");
						bank = address / 16;
						out << "        bank " << hexText(bank) << "\n";
						if (previous.compare(0, 3, "sb ") == 0 || previous.compare(0, 4, "snb ") == 0)
							unknownAfterNext = true;
					}
					continue;
				}
				if (!trimmed.empty() && trimmed[trimmed.size() - 1] == ':')
					bank = trimmed == "_start:" ? 0 : -1;
				out << line << "\n";
				if (trimmed.compare(0, 5, "call ") == 0)
					bank = -1;
				if (unknownAfterNext)
				{
					bank = -1;
					unknownAfterNext = false;
				}
				previous = trimmed;
			}
			return out.str();
		}

		string assembly(const map<string, int>& addresses)
		{
			ostringstream out;
			for (map<string, int>::const_iterator a = addresses.begin(); a != addresses.end(); ++a)
			{
				// an array is named by its address, a variable by its register
				bool isArray = arrayLabels.count(a->first) != 0;
				out << a->first << " equ " << hexText(isArray ? a->second : registerOf(a->second)) << "\n";
				if (addressTaken.count(a->first))
					out << a->first << "_addr equ " << hexText(a->second) << "\n";
			}
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

string generateAssembly(const Program& program, map<string, int>* variables, map<string, int>* highBytes)
{
	return Generator().run(program, variables, highBytes);
}
