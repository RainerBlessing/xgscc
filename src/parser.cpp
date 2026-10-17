#include "parser.h"
#include <set>
#include "errors.h"

using namespace std;

namespace
{
	const char* const ASSIGNMENTS[] = { "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=" };
	const char* const LATER_STATEMENTS[] = { "switch", "goto", "case", "default" };
	const char* const OTHER_TYPES[] = {
		"int", "char", "short", "long", "float", "double", "volatile",
		"uint32_t", "int32_t", "struct", "enum"
	};

	template <size_t N>
	bool contains(const char* const (&list)[N], const string& text)
	{
		for (size_t i = 0; i < N; i++)
		{
			if (text == list[i])
				return true;
		}
		return false;
	}

	// precedence of the binary operators, higher binds tighter
	int precedence(const string& op)
	{
		if (op == "||") return 1;
		if (op == "&&") return 2;
		if (op == "|") return 3;
		if (op == "^") return 4;
		if (op == "&") return 5;
		if (op == "==" || op == "!=") return 6;
		if (op == "<" || op == ">" || op == "<=" || op == ">=") return 7;
		if (op == "<<" || op == ">>") return 8;
		if (op == "+" || op == "-") return 9;
		if (op == "*" || op == "/" || op == "%") return 10;
		return 0;
	}

	class Parser
	{
	public:
		Parser(const vector<Token>& tokens) : tokens(tokens), position(0) {}

		Program program()
		{
			Program result;
			while (peek().type != Token::END)
				topLevel(&result);
			return result;
		}

	private:
		const vector<Token>& tokens;
		size_t position;

		const Token& peek(size_t offset = 0) const
		{
			size_t index = position + offset;
			return index < tokens.size() ? tokens[index] : tokens.back();
		}

		Token take()
		{
			Token token = peek();
			if (position < tokens.size() - 1)
				position++;
			return token;
		}

		bool isOperator(const char* text, size_t offset = 0) const
		{
			return peek(offset).type == Token::OPERATOR && peek(offset).text == text;
		}

		bool isWord(const char* text) const
		{
			return peek().type == Token::IDENT && peek().text == text;
		}

		static string describe(const Token& token)
		{
			return token.type == Token::END ? "the end of the file" : "'" + token.text + "'";
		}

		CompileError unexpected(const string& expected) const
		{
			return CompileError(peek().line, "expected " + expected + ", found " + describe(peek()));
		}

		void expectOperator(const char* text)
		{
			if (!isOperator(text))
				throw unexpected(string("'") + text + "'");
			take();
		}

		string expectName()
		{
			if (peek().type != Token::IDENT)
				throw unexpected("a name");
			return take().text;
		}

		// types

		bool isTypeStart() const
		{
			return peek().type == Token::IDENT &&
				(isTypeName(peek().text) || peek().text == "void" || peek().text == "static" || peek().text == "const" ||
				 contains(OTHER_TYPES, peek().text));
		}

		static bool isTypeName(const string& text)
		{
			return text == "uint8_t" || text == "int8_t" || text == "uint16_t" || text == "int16_t" ||
				text == "unsigned" || text == "signed";
		}

		struct Specifiers
		{
			Specifiers() : isStatic(false), isConst(false), isVoid(false) {}
			bool isStatic;
			bool isConst;
			bool isVoid;
			Type type;
		};

		// [static] [const] type, in any order of the qualifiers
		Specifiers specifiers()
		{
			Specifiers result;
			for (;;)
			{
				if (isWord("static") || isWord("const"))
				{
					bool& flag = isWord("static") ? result.isStatic : result.isConst;
					if (flag)
						throw CompileError(peek().line, "'" + peek().text + "' is written twice");
					flag = true;
					take();
				}
				else
					break;
			}
			if (isWord("void"))
			{
				take();
				result.isVoid = true;
				return result;
			}
			Token token = take();
			if (token.text == "uint8_t")
				result.type = Type(1, false);
			else if (token.text == "int8_t")
				result.type = Type(1, true);
			else if (token.text == "uint16_t")
				result.type = Type(2, false);
			else if (token.text == "int16_t")
				result.type = Type(2, true);
			else if ((token.text == "unsigned" || token.text == "signed") && isWord("char"))
			{
				take();
				result.type = Type(1, token.text == "signed");
			}
			else
				throw CompileError(token.line, "type '" + token.text + "' is not supported yet, use uint8_t, int8_t, uint16_t or int16_t");
			return result;
		}

		// ["*"] after the type: a pointer to it
		Type withStars(const Type& base)
		{
			if (!isOperator("*"))
				return base;
			take();
			if (isOperator("*"))
				throw CompileError(peek().line, "pointers to pointers are not supported yet");
			return pointerTo(base);
		}

		// declarations and functions

		void topLevel(Program* result)
		{
			if (!isTypeStart())
				throw CompileError(peek().line, "unexpected " + describe(peek()) + ", expected a declaration or a function");
			int line = peek().line;
			Specifiers type = specifiers();
			size_t stars = 0;
			while (isOperator("*", stars))
				stars++;
			if (peek(stars + 1).type == Token::OPERATOR && peek(stars + 1).text == "(")
			{
				Function function = functionAfterType(type);
				(function.body ? result->functions : result->prototypes).push_back(std::move(function));
				return;
			}
			if (type.isVoid)
				throw CompileError(line, "a variable cannot be void");
			result->globals.push_back(declarationAfterType(line, type));
		}

		// name ( parameters ) { body }   or   name ( parameters ) ;
		Function functionAfterType(const Specifiers& type)
		{
			Function result;
			result.line = peek().line;
			result.returnsValue = !type.isVoid;
			result.returnType = withStars(type.type);
			if (type.isVoid && result.returnType.isPointer)
				throw CompileError(result.line, "a pointer to void is not supported");
			result.name = expectName();
			expectOperator("(");
			if (isWord("void") && isOperator(")", 1))
				take();
			else if (!isOperator(")"))
			{
				for (;;)
				{
					Parameter parameter;
					parameter.line = peek().line;
					Specifiers parameterType = specifiers();
					if (parameterType.isVoid)
						throw CompileError(parameter.line, "a parameter cannot be void");
					parameter.type = withStars(parameterType.type);
					parameter.name = expectName();
					if (isOperator("["))
						throw CompileError(peek().line, "array parameters are not supported, use a pointer");
					result.parameters.push_back(parameter);
					if (!isOperator(","))
						break;
					take();
				}
			}
			expectOperator(")");
			if (isOperator(";"))
			{
				take();
				return result;
			}
			result.body = block();
			return result;
		}

		// [*] name [ [size] ] [= initializer] ; with the type already read
		StmtPtr declarationAfterType(int line, const Specifiers& specifiers, bool endsWithSemicolon = true)
		{
			if (specifiers.isVoid)
				throw CompileError(line, "a variable cannot be void");
			StmtPtr declaration(new Stmt(Stmt::DECLARATION, line));
			declaration->type = withStars(specifiers.type);
			declaration->isStatic = specifiers.isStatic;
			// "const uint8_t *p" is a pointer to constants, the pointer itself can change
			declaration->isConst = specifiers.isConst && !declaration->type.isPointer;
			declaration->name = expectName();
			bool sizeGiven = false;
			if (isOperator("["))
			{
				take();
				if (peek().type == Token::NUMBER)
				{
					declaration->arraySize = take().value;
					sizeGiven = true;
					if (declaration->arraySize <= 0)
						throw CompileError(line, "the size of the array '" + declaration->name + "' must be positive");
				}
				else if (!isOperator("]"))
					throw CompileError(peek().line, "the size of an array must be a number");
				expectOperator("]");
				declaration->arraySize = sizeGiven ? declaration->arraySize : -1;
			}
			if (isOperator("="))
			{
				take();
				if (declaration->arraySize != 0)
					initializerList(*declaration, sizeGiven);
				else if (isOperator("{"))
					throw CompileError(peek().line, "an initializer list is only for arrays");
				else if (peek().type == Token::STRING)
					throw CompileError(peek().line, "a string can only initialize an array");
				else
					declaration->expr = expression();
			}
			if (declaration->arraySize == -1)
				throw CompileError(line, "the size of the array '" + declaration->name + "' is missing");
			if (endsWithSemicolon)
				expectOperator(";");
			return declaration;
		}

		// { value, value, ... } of an array; without a size the list gives it
		void initializerList(Stmt& declaration, bool sizeGiven)
		{
			if (peek().type == Token::STRING)
			{
				stringInitializer(declaration, sizeGiven);
				return;
			}
			if (!isOperator("{"))
				throw CompileError(peek().line, "an array needs an initializer list in { }");
			take();
			while (!isOperator("}"))
			{
				declaration.elements.push_back(expression());
				if (!isOperator(","))
					break;
				take();
			}
			expectOperator("}");
			int count = (int) declaration.elements.size();
			if (!sizeGiven)
				declaration.arraySize = count;
			else if (count > declaration.arraySize)
				throw CompileError(declaration.line, "too many initializers for the array '" + declaration.name + "'");
		}

		// statements

		StmtPtr block()
		{
			StmtPtr result(new Stmt(Stmt::BLOCK, peek().line));
			expectOperator("{");
			while (!isOperator("}"))
			{
				if (peek().type == Token::END)
					throw CompileError(peek().line, "the end of the file is reached, '}' is missing");
				result->statements.push_back(statement());
			}
			take();
			return result;
		}

		StmtPtr statement()
		{
			int line = peek().line;
			if (isOperator(";"))
			{
				take();
				return StmtPtr(new Stmt(Stmt::EMPTY, line));
			}
			if (isOperator("{"))
				return block();
			if (isTypeStart())
			{
				Specifiers type = specifiers();
				return declarationAfterType(line, type);
			}
			if (peek().type == Token::IDENT)
			{
				if (peek().text == "if")
					return ifStatement();
				if (peek().text == "while")
					return whileStatement();
				if (peek().text == "for")
					return forStatement();
				if (peek().text == "do")
					return doStatement();
				if (peek().text == "return")
					return returnStatement();
				if (peek().text == "break" || peek().text == "continue")
				{
					StmtPtr jump(new Stmt(peek().text == "break" ? Stmt::BREAK : Stmt::CONTINUE, line));
					take();
					expectOperator(";");
					return jump;
				}
				if (contains(LATER_STATEMENTS, peek().text))
					throw CompileError(line, "'" + peek().text + "' is not supported yet");
			}
			StmtPtr result = simpleStatement();
			expectOperator(";");
			return result;
		}

		StmtPtr ifStatement()
		{
			StmtPtr result(new Stmt(Stmt::IF, take().line));
			expectOperator("(");
			result->expr = expression();
			expectOperator(")");
			result->body = statement();
			if (isWord("else"))
			{
				take();
				result->elseBody = statement();
			}
			return result;
		}

		StmtPtr whileStatement()
		{
			StmtPtr result(new Stmt(Stmt::WHILE, take().line));
			expectOperator("(");
			result->expr = expression();
			expectOperator(")");
			result->body = statement();
			return result;
		}

		StmtPtr doStatement()
		{
			StmtPtr result(new Stmt(Stmt::DO_WHILE, take().line));
			result->body = statement();
			if (!isWord("while"))
				throw unexpected("'while' after the body of do");
			take();
			expectOperator("(");
			result->expr = expression();
			expectOperator(")");
			expectOperator(";");
			return result;
		}

		StmtPtr returnStatement()
		{
			StmtPtr result(new Stmt(Stmt::RETURN, take().line));
			if (!isOperator(";"))
				result->expr = expression();
			expectOperator(";");
			return result;
		}

		StmtPtr forStatement()
		{
			StmtPtr result(new Stmt(Stmt::FOR, take().line));
			expectOperator("(");
			if (isTypeStart())
			{
				int line = peek().line;
				Specifiers type = specifiers();
				result->init = declarationAfterType(line, type, false);
			}
			else if (!isOperator(";"))
				result->init = simpleStatement();
			expectOperator(";");
			if (!isOperator(";"))
				result->expr = expression();
			expectOperator(";");
			if (!isOperator(")"))
				result->step = simpleStatement();
			expectOperator(")");
			result->body = statement();
			return result;
		}

		// "text" for an array of bytes: the characters and, if there is room, a 0
		void stringInitializer(Stmt& declaration, bool sizeGiven)
		{
			Token text = take();
			if (declaration.type.size != 1 || declaration.type.isPointer)
				throw CompileError(text.line, "a string can only initialize an array of 8 bit integers");
			for (size_t i = 0; i < text.text.size(); i++)
			{
				ExprPtr character(new Expr(Expr::NUMBER, text.line));
				character->value = (unsigned char) text.text[i];
				declaration.elements.push_back(std::move(character));
			}
			int characters = (int) declaration.elements.size();
			if (!sizeGiven || characters < declaration.arraySize)
			{
				declaration.elements.push_back(ExprPtr(new Expr(Expr::NUMBER, text.line)));
				characters++;
			}
			if (!sizeGiven)
				declaration.arraySize = characters;
			else if ((int) declaration.elements.size() > declaration.arraySize)
				throw CompileError(declaration.line, "too many initializers for the array '" + declaration.name + "'");
		}

		static bool isAssignable(const Expr& expr)
		{
			return expr.kind == Expr::VARIABLE || expr.kind == Expr::INDEX || (expr.kind == Expr::UNARY && expr.name == "*");
		}

		// the variable or the *p and a[i] that a statement changes
		static void setTarget(Stmt& stmt, ExprPtr target)
		{
			if (target->kind == Expr::VARIABLE)
				stmt.name = target->name;
			else
				stmt.target = std::move(target);
		}

		// an assignment, ++/-- or a call, without the semicolon
		StmtPtr simpleStatement()
		{
			int line = peek().line;
			if (isOperator("++") || isOperator("--"))
			{
				StmtPtr result(new Stmt(Stmt::STEP, line));
				result->op = take().text;
				ExprPtr target = unary();
				if (!isAssignable(*target))
					throw CompileError(line, "cannot assign to this expression");
				setTarget(*result, std::move(target));
				return result;
			}
			if (peek().type != Token::IDENT && !isOperator("*") && !isOperator("("))
				throw CompileError(line, "expected a statement, found " + describe(peek()));
			ExprPtr target = unary();
			if (target->kind == Expr::CALL)
			{
				StmtPtr result(new Stmt(Stmt::CALL, line));
				result->expr = std::move(target);
				return result;
			}
			if (peek().type == Token::OPERATOR && (peek().text == "++" || peek().text == "--"))
			{
				if (target->kind == Expr::UNARY && target->name == "*" && !target->parenthesized)
					throw CompileError(line, "'*p++' is not supported, write (*p)++ or p++");
				if (!isAssignable(*target))
					throw CompileError(line, "cannot assign to this expression");
				StmtPtr result(new Stmt(Stmt::STEP, line));
				result->op = take().text;
				setTarget(*result, std::move(target));
				return result;
			}
			if (peek().type != Token::OPERATOR || !contains(ASSIGNMENTS, peek().text))
			{
				if (target->kind == Expr::VARIABLE)
					throw CompileError(line, "'" + target->name + "' is no statement, expected an assignment, ++ or --");
				throw CompileError(line, "this expression is no statement, expected an assignment, ++ or --");
			}
			if (!isAssignable(*target))
				throw CompileError(line, "cannot assign to this expression");
			StmtPtr result(new Stmt(Stmt::ASSIGNMENT, line));
			result->op = take().text;
			setTarget(*result, std::move(target));
			result->expr = expression();
			return result;
		}

		// expressions

		ExprPtr expression()
		{
			ExprPtr result = binary(1);
			if (peek().type == Token::OPERATOR)
			{
				const string& op = peek().text;
				if (contains(ASSIGNMENTS, op))
					throw CompileError(peek().line, "an assignment is a statement and cannot be used in an expression");
				if (op == "++" || op == "--")
					throw CompileError(peek().line, "'" + op + "' is a statement and cannot be used in an expression");
			}
			return result;
		}

		ExprPtr binary(int minimum)
		{
			ExprPtr left = unary();
			for (;;)
			{
				if (peek().type != Token::OPERATOR)
					return left;
				string op = peek().text;
				int level = precedence(op);
				if (level < minimum || level == 0)
					return left;
				int line = take().line;
				ExprPtr node(new Expr(Expr::BINARY, line));
				node->name = op;
				node->left = std::move(left);
				node->right = binary(level + 1);
				left = std::move(node);
			}
		}

		ExprPtr unary()
		{
			const Token& token = peek();
			if (token.type == Token::OPERATOR && (token.text == "-" || token.text == "~" || token.text == "!"))
			{
				ExprPtr node(new Expr(Expr::UNARY, token.line));
				node->name = take().text;
				node->left = unary();
				return node;
			}
			if (token.type == Token::OPERATOR && (token.text == "*" || token.text == "&"))
			{
				ExprPtr node(new Expr(Expr::UNARY, token.line));
				node->name = take().text;
				node->left = unary();
				return node;
			}
			if (isOperator("(") && peek(1).type == Token::IDENT && (isTypeName(peek(1).text) ||
					peek(1).text == "void" || contains(OTHER_TYPES, peek(1).text)))
				return cast();
			return postfix();
		}

		// name ( arguments )
		ExprPtr call()
		{
			ExprPtr node(new Expr(Expr::CALL, peek().line));
			node->name = take().text;
			expectOperator("(");
			if (!isOperator(")"))
			{
				for (;;)
				{
					node->arguments.push_back(expression());
					if (!isOperator(","))
						break;
					take();
				}
			}
			expectOperator(")");
			return node;
		}

		// primary [ index ] ...
		ExprPtr postfix()
		{
			ExprPtr node = primary();
			while (isOperator("["))
			{
				ExprPtr index(new Expr(Expr::INDEX, take().line));
				index->left = std::move(node);
				index->right = expression();
				expectOperator("]");
				node = std::move(index);
			}
			return node;
		}

		// ( type ) operand
		ExprPtr cast()
		{
			ExprPtr node(new Expr(Expr::CAST, take().line));
			Specifiers type = specifiers();
			if (type.isVoid)
				throw CompileError(node->line, "a cast to void is not supported");
			node->type = withStars(type.type);
			expectOperator(")");
			node->left = unary();
			return node;
		}

		ExprPtr primary()
		{
			const Token& token = peek();
			if (token.type == Token::NUMBER)
			{
				ExprPtr node(new Expr(Expr::NUMBER, token.line));
				node->value = take().value;
				return node;
			}
			if (token.type == Token::IDENT)
			{
				if (isOperator("(", 1))
					return call();
				ExprPtr node(new Expr(Expr::VARIABLE, token.line));
				node->name = take().text;
				return node;
			}
			if (isOperator("("))
			{
				take();
				ExprPtr inner = binary(1);
				expectOperator(")");
				inner->parenthesized = true;
				return inner;
			}
			if (token.type == Token::STRING)
				throw CompileError(token.line, "a string can only initialize an array");
			throw unexpected("an expression");
		}
	};
}

Program parse(const vector<Token>& tokens)
{
	return Parser(tokens).program();
}
