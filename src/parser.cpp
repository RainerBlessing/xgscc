#include "parser.h"
#include <set>
#include "errors.h"

using namespace std;

namespace
{
	const char* const ASSIGNMENTS[] = { "=", "+=", "-=", "&=", "|=", "^=", "<<=", ">>=" };
	const char* const LATER_ASSIGNMENTS[] = { "*=", "/=", "%=" };
	const char* const LATER_STATEMENTS[] = { "do", "switch", "break", "continue", "return", "goto", "case", "default" };
	const char* const OTHER_TYPES[] = {
		"int", "char", "short", "long", "signed", "float", "double", "const", "static", "volatile",
		"uint16_t", "int8_t", "int16_t", "uint32_t", "int32_t", "void", "struct", "enum"
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
				(peek().text == "uint8_t" || peek().text == "unsigned" || contains(OTHER_TYPES, peek().text));
		}

		void type()
		{
			Token token = take();
			if (token.text == "uint8_t")
				return;
			if (token.text == "unsigned" && isWord("char"))
			{
				take();
				return;
			}
			throw CompileError(token.line, "type '" + token.text + "' is not supported yet, use uint8_t");
		}

		// declarations and functions

		void topLevel(Program* result)
		{
			if (isWord("void"))
			{
				result->functions.push_back(function());
				return;
			}
			if (!isTypeStart())
				throw CompileError(peek().line, "unexpected " + describe(peek()) + ", expected a declaration or a function");
			int line = peek().line;
			type();
			if (peek(1).type == Token::OPERATOR && peek(1).text == "(")
				throw CompileError(line, "function " + peek().text + " must return void, other types are not supported yet");
			result->globals.push_back(declarationAfterType(line));
		}

		Function function()
		{
			Function result;
			result.line = take().line;		// void
			result.name = expectName();
			expectOperator("(");
			if (isWord("void"))
				take();
			if (!isOperator(")"))
				throw CompileError(peek().line, "parameters are not supported yet");
			take();
			result.body = block();
			return result;
		}

		// name [= expression] ; with the type already read
		StmtPtr declarationAfterType(int line, bool endsWithSemicolon = true)
		{
			StmtPtr declaration(new Stmt(Stmt::DECLARATION, line));
			declaration->name = expectName();
			if (isOperator("="))
			{
				take();
				declaration->expr = expression();
			}
			if (endsWithSemicolon)
				expectOperator(";");
			return declaration;
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
				type();
				return declarationAfterType(line);
			}
			if (peek().type == Token::IDENT)
			{
				if (peek().text == "if")
					return ifStatement();
				if (peek().text == "while")
					return whileStatement();
				if (peek().text == "for")
					return forStatement();
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

		StmtPtr forStatement()
		{
			StmtPtr result(new Stmt(Stmt::FOR, take().line));
			expectOperator("(");
			if (isTypeStart())
			{
				int line = peek().line;
				type();
				result->init = declarationAfterType(line, false);
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

		// an assignment or ++/--, without the semicolon
		StmtPtr simpleStatement()
		{
			int line = peek().line;
			if (isOperator("++") || isOperator("--"))
			{
				StmtPtr result(new Stmt(Stmt::STEP, line));
				result->op = take().text;
				result->name = expectName();
				return result;
			}
			if (peek().type != Token::IDENT)
				throw CompileError(line, "expected a statement, found " + describe(peek()));
			string name = take().text;
			if (isOperator("++") || isOperator("--"))
			{
				StmtPtr result(new Stmt(Stmt::STEP, line));
				result->name = name;
				result->op = take().text;
				return result;
			}
			if (contains(LATER_ASSIGNMENTS, peek().text) && peek().type == Token::OPERATOR)
				throw CompileError(line, "operator '" + peek().text + "' is not supported yet");
			if (peek().type != Token::OPERATOR || !contains(ASSIGNMENTS, peek().text))
				throw CompileError(line, "'" + name + "' is no statement, expected an assignment, ++ or --");
			StmtPtr result(new Stmt(Stmt::ASSIGNMENT, line));
			result->name = name;
			result->op = take().text;
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
				if (contains(ASSIGNMENTS, op) || contains(LATER_ASSIGNMENTS, op))
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
				if (op == "*" || op == "/" || op == "%")
					throw CompileError(peek().line, "operator '" + op + "' is not supported yet");
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
				throw CompileError(token.line, "pointers are not supported yet");
			return primary();
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
					throw CompileError(token.line, "function calls are not supported yet");
				ExprPtr node(new Expr(Expr::VARIABLE, token.line));
				node->name = take().text;
				return node;
			}
			if (isOperator("("))
			{
				take();
				ExprPtr inner = binary(1);
				expectOperator(")");
				return inner;
			}
			throw unexpected("an expression");
		}
	};
}

Program parse(const vector<Token>& tokens)
{
	return Parser(tokens).program();
}
