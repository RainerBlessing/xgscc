#include "lexer.h"
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include "errors.h"

using namespace std;

namespace
{
	// longest first, so that "<<=" is not read as "<<" and "="
	const char* const OPERATORS[] = {
		"<<=", ">>=",
		"++", "--", "->", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
		"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
		"+", "-", "*", "/", "%", "&", "|", "^", "~", "!", "<", ">", "=",
		"(", ")", "{", "}", "[", "]", ";", ",", ".", "?", ":"
	};

	bool isIdentStart(char c)
	{
		return isalpha((unsigned char) c) || c == '_';
	}

	bool isIdentPart(char c)
	{
		return isalnum((unsigned char) c) || c == '_';
	}

	struct Macro
	{
		Macro() : function(false) {}
		bool function;
		vector<string> params;
		vector<Token> body;
	};

	// what all the files of one run share
	struct State
	{
		map<string, Macro> macros;
		set<string> includes;
	};

	const int MAX_INCLUDE_DEPTH = 16;

	string trimmed(const string& text)
	{
		size_t first = text.find_first_not_of(" \t\r");
		if (first == string::npos)
			return "";
		return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
	}

	bool isOperator(const Token& token, const char* text)
	{
		return token.type == Token::OPERATOR && token.text == text;
	}

	class Lexer
	{
	public:
		// expandMacros false: the names of macros stay names (for the arguments of a call and the
		// text of #if, which expand them later)
		Lexer(const string& source, const string& includeDir, State* state, int depth = 0, bool expandMacros = true, int firstLine = 1) :
			source(source), includeDir(includeDir), state(state), depth(depth), expandMacros(expandMacros), position(0), line(firstLine), lineStart(true) {}

		vector<Token> run()
		{
			vector<Token> tokens;
			for (;;)
			{
				skipSpaceAndComments();
				if (position >= source.size())
					break;
				char c = source[position];
				if (c == '#' && lineStart && expandMacros)
					directive(&tokens);
				else if (!active())
					skipLine();
				else if (isdigit((unsigned char) c))
					tokens.push_back(number());
				else if (c == '\'')
					tokens.push_back(character());
				else if (c == '"')
					tokens.push_back(stringLiteral());
				else if (isIdentStart(c))
					identifier(&tokens);
				else
					tokens.push_back(operatorToken());
				lineStart = false;
			}
			if (!conditions.empty())
				throw CompileError(conditions.back().line, "#if without #endif");
			Token end = { Token::END, "", 0, line };
			tokens.push_back(end);
			return tokens;
		}

	private:
		struct Condition
		{
			bool parentActive;	// the text around the #if is used
			bool taken;		// a branch has been used already
			bool active;		// the text now is used
			bool seenElse;
			int line;
		};

		string source;
		string includeDir;
		State* state;
		int depth;
		bool expandMacros;
		size_t position;
		int line;
		bool lineStart;		// only blanks since the last line break
		vector<Condition> conditions;

		bool active() const
		{
			return conditions.empty() || conditions.back().active;
		}

		char peek(size_t offset = 0) const
		{
			return position + offset < source.size() ? source[position + offset] : '\0';
		}

		void skipLine()
		{
			while (position < source.size() && source[position] != '\n')
				position++;
		}

		void skipSpaceAndComments()
		{
			while (position < source.size())
			{
				char c = source[position];
				if (c == '\n')
				{
					line++;
					lineStart = true;
					position++;
				}
				else if (isspace((unsigned char) c))
					position++;
				else if (c == '/' && peek(1) == '/')
				{
					while (position < source.size() && source[position] != '\n')
						position++;
				}
				else if (c == '/' && peek(1) == '*')
				{
					int start = line;
					position += 2;
					while (!(peek() == '*' && peek(1) == '/'))
					{
						if (position >= source.size())
							throw CompileError(start, "comment is not closed");
						if (source[position] == '\n')
							line++;
						position++;
					}
					position += 2;
				}
				else
					break;
			}
		}

		void directive(vector<Token>* tokens)
		{
			int directiveLine = line;
			position++;
			while (peek() == ' ' || peek() == '\t')
				position++;
			size_t nameStart = position;
			while (isIdentPart(peek()))
				position++;
			string name = source.substr(nameStart, position - nameStart);
			string rest = restOfLine();

			// the conditions are followed in the text that is skipped, too
			if (name == "if" || name == "ifdef" || name == "ifndef")
			{
				Condition condition = { active(), false, false, false, directiveLine };
				if (condition.parentActive)
				{
					bool value = name == "if" ? evaluate(rest, directiveLine) : state->macros.count(macroName(rest, name)) != 0;
					if (name == "ifndef")
						value = !value;
					condition.taken = value;
					condition.active = value;
				}
				conditions.push_back(condition);
				return;
			}
			if (name == "elif" || name == "else")
			{
				if (conditions.empty())
					throw CompileError(directiveLine, "#" + name + " without #if");
				Condition& condition = conditions.back();
				if (condition.seenElse)
					throw CompileError(directiveLine, "#" + name + " after #else");
				if (name == "else")
				{
					condition.seenElse = true;
					condition.active = condition.parentActive && !condition.taken;
					condition.taken = true;
				}
				else if (!condition.parentActive || condition.taken)
					condition.active = false;
				else
				{
					condition.active = evaluate(rest, directiveLine);
					condition.taken = condition.active;
				}
				return;
			}
			if (name == "endif")
			{
				if (conditions.empty())
					throw CompileError(directiveLine, "#endif without #if");
				conditions.pop_back();
				return;
			}
			if (!active())
				return;

			if (name == "include")
				include(rest, directiveLine, tokens);
			else if (name == "define")
				define(rest);
			else if (name == "undef")
				state->macros.erase(macroName(rest, name));
			else if (name == "error")
				throw CompileError(directiveLine, "#error " + trimmed(rest));
			else
				throw CompileError(directiveLine, "#" + name + " is not supported");
		}

		string restOfLine()
		{
			size_t start = position;
			while (position < source.size() && source[position] != '\n')
				position++;
			return source.substr(start, position - start);
		}

		// #include <name> is built in, #include "file" reads the file (if there is a directory to look in)
		void include(const string& rest, int directiveLine, vector<Token>* tokens)
		{
			string text = trimmed(rest);
			size_t comment = text.find("//");
			if (comment != string::npos)
				text = trimmed(text.substr(0, comment));
			if (text.size() < 2 || (text[0] != '<' && text[0] != '"'))
				throw CompileError(directiveLine, "#include needs <name> or \"name\"");
			char closing = text[0] == '<' ? '>' : '"';
			size_t end = text.find(closing, 1);
			if (end == string::npos)
				throw CompileError(directiveLine, "#include needs <name> or \"name\"");
			string name = text.substr(1, end - 1);
			state->includes.insert(name);
			if (text[0] == '<' || includeDir.empty())
				return;
			if (depth >= MAX_INCLUDE_DEPTH)
				throw CompileError(directiveLine, "#include of " + name + " is nested too deeply");
			string path = includeDir + "/" + name;
			ifstream file(path.c_str());
			if (!file)
				throw CompileError(directiveLine, "cannot open the include file " + name);
			stringstream content;
			content << file.rdbuf();
			size_t slash = path.find_last_of('/');
			Lexer nested(content.str(), path.substr(0, slash), state, depth + 1);
			vector<Token> inner = nested.run();
			inner.pop_back();		// END
			tokens->insert(tokens->end(), inner.begin(), inner.end());
		}

		// the name at the start of a directive's text
		string macroName(const string& text, const string& directive) const
		{
			size_t first = text.find_first_not_of(" \t");
			if (first == string::npos || !isIdentStart(text[first]))
				throw CompileError(line, "a name is missing after #" + directive);
			size_t end = first;
			while (end < text.size() && isIdentPart(text[end]))
				end++;
			return text.substr(first, end - first);
		}

		vector<Token> tokensOf(const string& text, int firstLine) const
		{
			Lexer inner(text, "", state, 0, false, firstLine);
			vector<Token> tokens = inner.run();
			tokens.pop_back();		// END
			return tokens;
		}

		void define(const string& text)
		{
			string name = macroName(text, "define");
			size_t afterName = text.find(name) + name.size();
			Macro macro;
			string body;
			if (afterName < text.size() && text[afterName] == '(')
			{
				macro.function = true;
				size_t close = text.find(')', afterName);
				if (close == string::npos)
					throw CompileError(line, "bad parameter list in #define " + name);
				string list = text.substr(afterName + 1, close - afterName - 1);
				if (!trimmed(list).empty())
				{
					size_t from = 0;
					for (;;)
					{
						size_t comma = list.find(',', from);
						string parameter = trimmed(list.substr(from, comma == string::npos ? string::npos : comma - from));
						bool valid = !parameter.empty() && isIdentStart(parameter[0]);
						for (size_t i = 0; valid && i < parameter.size(); i++)
							valid = isIdentPart(parameter[i]);
						if (!valid)
							throw CompileError(line, "bad parameter list in #define " + name);
						for (size_t i = 0; i < macro.params.size(); i++)
						{
							if (macro.params[i] == parameter)
								throw CompileError(line, "parameter " + parameter + " of " + name + " is used twice");
						}
						macro.params.push_back(parameter);
						if (comma == string::npos)
							break;
						from = comma + 1;
					}
				}
				body = text.substr(close + 1);
			}
			else
				body = text.substr(afterName);
			macro.body = tokensOf(body, line);
			state->macros[name] = macro;
		}

		// the macros in the tokens are replaced; names in `active` are being expanded and stay
		vector<Token> expandTokens(const vector<Token>& in, set<string>* active) const
		{
			vector<Token> out;
			for (size_t i = 0; i < in.size(); i++)
			{
				const Token& token = in[i];
				map<string, Macro>::const_iterator found = token.type == Token::IDENT ? state->macros.find(token.text) : state->macros.end();
				if (found == state->macros.end() || active->count(token.text))
				{
					out.push_back(token);
					continue;
				}
				const Macro& macro = found->second;
				vector<Token> replacement;
				size_t last = i;
				if (!macro.function)
					replacement = macro.body;
				else
				{
					if (i + 1 >= in.size() || !isOperator(in[i + 1], "("))
					{
						out.push_back(token);
						continue;
					}
					vector<vector<Token> > arguments(1);
					int nesting = 1;
					size_t at = i + 2;
					for (; at < in.size() && nesting > 0; at++)
					{
						if (isOperator(in[at], "("))
							nesting++;
						else if (isOperator(in[at], ")"))
						{
							nesting--;
							if (nesting == 0)
								break;
						}
						if (nesting == 1 && isOperator(in[at], ","))
							arguments.push_back(vector<Token>());
						else
							arguments.back().push_back(in[at]);
					}
					if (nesting > 0)
						throw CompileError(token.line, "the call of the macro " + token.text + " is not closed");
					last = at;
					if (macro.params.empty() && arguments.size() == 1 && arguments[0].empty())
						arguments.clear();
					if (arguments.size() != macro.params.size())
						throw CompileError(token.line, "the macro " + token.text + " takes " + decimal(macro.params.size()) + " arguments, " +
							decimal(arguments.size()) + " given");
					vector<vector<Token> > expanded;
					for (size_t a = 0; a < arguments.size(); a++)
						expanded.push_back(expandTokens(arguments[a], active));
					for (size_t b = 0; b < macro.body.size(); b++)
					{
						const Token& bodyToken = macro.body[b];
						size_t parameter = macro.params.size();
						if (bodyToken.type == Token::IDENT)
						{
							for (size_t p = 0; p < macro.params.size(); p++)
							{
								if (macro.params[p] == bodyToken.text)
									parameter = p;
							}
						}
						if (parameter < macro.params.size())
							replacement.insert(replacement.end(), expanded[parameter].begin(), expanded[parameter].end());
						else
							replacement.push_back(bodyToken);
					}
				}
				for (size_t r = 0; r < replacement.size(); r++)
					replacement[r].line = token.line;
				active->insert(token.text);
				vector<Token> result = expandTokens(replacement, active);
				active->erase(token.text);
				out.insert(out.end(), result.begin(), result.end());
				i = last;
			}
			return out;
		}

		static string decimal(size_t value)
		{
			ostringstream out;
			out << value;
			return out.str();
		}

		// #if: defined(X) first, then the macros, what is left of the names is 0
		bool evaluate(const string& text, int directiveLine)
		{
			vector<Token> raw = tokensOf(text, directiveLine);
			vector<Token> tokens;
			for (size_t i = 0; i < raw.size(); i++)
			{
				if (raw[i].type == Token::IDENT && raw[i].text == "defined")
				{
					bool parenthesis = i + 1 < raw.size() && isOperator(raw[i + 1], "(");
					size_t nameAt = i + (parenthesis ? 2 : 1);
					if (nameAt >= raw.size() || raw[nameAt].type != Token::IDENT || (parenthesis && (nameAt + 1 >= raw.size() || !isOperator(raw[nameAt + 1], ")"))))
						throw CompileError(directiveLine, "bad expression in #if: defined needs a name");
					Token number = { Token::NUMBER, "", state->macros.count(raw[nameAt].text) ? 1 : 0, directiveLine };
					tokens.push_back(number);
					i = nameAt + (parenthesis ? 1 : 0);
				}
				else
					tokens.push_back(raw[i]);
			}
			set<string> noneActive;
			tokens = expandTokens(tokens, &noneActive);
			for (size_t i = 0; i < tokens.size(); i++)
			{
				if (tokens[i].type == Token::IDENT)
				{
					tokens[i].type = Token::NUMBER;
					tokens[i].value = 0;
				}
			}
			if (tokens.empty())
				throw CompileError(directiveLine, "#if needs an expression");
			ConditionParser parser(tokens, directiveLine);
			return parser.run() != 0;
		}

		// the expression of an #if with the operators of C
		class ConditionParser
		{
		public:
			ConditionParser(const vector<Token>& tokens, int line) : tokens(tokens), at(0), line(line) {}

			long run()
			{
				long value = ternary(true);
				if (at < tokens.size())
					fail();
				return value;
			}

		private:
			const vector<Token>& tokens;
			size_t at;
			int line;

			void fail() const
			{
				throw CompileError(line, "bad expression in #if");
			}

			bool next(const char* op)
			{
				if (at < tokens.size() && isOperator(tokens[at], op))
				{
					at++;
					return true;
				}
				return false;
			}

			long ternary(bool live)
			{
				long condition = logicalOr(live);
				if (!next("?"))
					return condition;
				long yes = ternary(live && condition != 0);
				if (!next(":"))
					fail();
				long no = ternary(live && condition == 0);
				return condition != 0 ? yes : no;
			}

			long logicalOr(bool live)
			{
				long value = logicalAnd(live);
				while (next("||"))
				{
					long right = logicalAnd(live && value == 0);
					value = (value != 0 || right != 0) ? 1 : 0;
				}
				return value;
			}

			long logicalAnd(bool live)
			{
				long value = bitOr(live);
				while (next("&&"))
				{
					long right = bitOr(live && value != 0);
					value = (value != 0 && right != 0) ? 1 : 0;
				}
				return value;
			}

			long bitOr(bool live)
			{
				long value = bitXor(live);
				while (next("|"))
					value |= bitXor(live);
				return value;
			}

			long bitXor(bool live)
			{
				long value = bitAnd(live);
				while (next("^"))
					value ^= bitAnd(live);
				return value;
			}

			long bitAnd(bool live)
			{
				long value = equality(live);
				while (next("&"))
					value &= equality(live);
				return value;
			}

			long equality(bool live)
			{
				long value = relation(live);
				for (;;)
				{
					if (next("=="))
						value = value == relation(live);
					else if (next("!="))
						value = value != relation(live);
					else
						return value;
				}
			}

			long relation(bool live)
			{
				long value = shift(live);
				for (;;)
				{
					if (next("<="))
						value = value <= shift(live);
					else if (next(">="))
						value = value >= shift(live);
					else if (next("<"))
						value = value < shift(live);
					else if (next(">"))
						value = value > shift(live);
					else
						return value;
				}
			}

			long shift(bool live)
			{
				long value = sum(live);
				for (;;)
				{
					if (next("<<"))
					{
						long amount = sum(live);
						value = amount < 0 || amount > 31 ? 0 : value << amount;
					}
					else if (next(">>"))
					{
						long amount = sum(live);
						value = amount < 0 || amount > 31 ? 0 : value >> amount;
					}
					else
						return value;
				}
			}

			long sum(bool live)
			{
				long value = product(live);
				for (;;)
				{
					if (next("+"))
						value += product(live);
					else if (next("-"))
						value -= product(live);
					else
						return value;
				}
			}

			long product(bool live)
			{
				long value = unary(live);
				for (;;)
				{
					if (next("*"))
					{
						value *= unary(live);
						continue;
					}
					bool divide = next("/");
					bool remainder = !divide && next("%");
					if (!divide && !remainder)
						return value;
					long right = unary(live);
					if (right == 0)
					{
						if (live)
							throw CompileError(line, "division by zero in #if");
						value = 0;
					}
					else
						value = divide ? value / right : value % right;
				}
			}

			long unary(bool live)
			{
				if (next("!"))
					return unary(live) == 0;
				if (next("~"))
					return ~unary(live);
				if (next("-"))
					return -unary(live);
				if (next("+"))
					return unary(live);
				if (next("("))
				{
					long value = ternary(live);
					if (!next(")"))
						fail();
					return value;
				}
				if (at < tokens.size() && tokens[at].type == Token::NUMBER)
					return tokens[at++].value;
				fail();
				return 0;
			}
		};

		Token number()
		{
			size_t start = position;
			long value = 0;
			if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X'))
			{
				position += 2;
				if (!isxdigit((unsigned char) peek()))
					throw CompileError(line, "bad number " + source.substr(start, position - start));
				while (isxdigit((unsigned char) peek()))
				{
					char c = tolower((unsigned char) source[position++]);
					value = value * 16 + (isdigit((unsigned char) c) ? c - '0' : c - 'a' + 10);
					checkSize(value);
				}
			}
			else
			{
				while (isdigit((unsigned char) peek()))
				{
					value = value * 10 + (source[position++] - '0');
					checkSize(value);
				}
			}
			size_t digitsEnd = position;
			if (peek() == 'u' || peek() == 'U')
				position++;
			if (isIdentPart(peek()) || peek() == '.')
			{
				while (isIdentPart(peek()) || peek() == '.')
					position++;
				throw CompileError(line, "bad number " + source.substr(start, position - start));
			}
			Token token = { Token::NUMBER, source.substr(start, digitsEnd - start), (int) value, line };
			return token;
		}

		void checkSize(long value) const
		{
			if (value > 0xffff)
				throw CompileError(line, "number is too large");
		}

		// the value of the character after a backslash
		int escape(char c)
		{
			switch (c)
			{
			case 'n': return '\n';
			case 't': return '\t';
			case 'r': return '\r';
			case '0': return 0;
			case '\\': return '\\';
			case '\'': return '\'';
			case '"': return '"';
			default: throw CompileError(line, string("unknown escape \\") + c);
			}
		}

		Token stringLiteral()
		{
			int start = line;
			position++;
			std::string characters;
			for (;;)
			{
				char c = peek();
				if (c == '"')
					break;
				if (c == '\0' || c == '\n')
					throw CompileError(start, "the string is not closed");
				if (c == '\\')
				{
					characters += (char) escape(peek(1));
					position += 2;
				}
				else
				{
					characters += c;
					position++;
				}
			}
			position++;
			Token token = { Token::STRING, characters, 0, start };
			return token;
		}

		Token character()
		{
			size_t start = position++;
			int value;
			char c = peek();
			if (c == '\\')
			{
				char escape = peek(1);
				position += 2;
				switch (escape)
				{
				case 'n': value = '\n'; break;
				case 't': value = '\t'; break;
				case 'r': value = '\r'; break;
				case '0': value = 0; break;
				case '\\': value = '\\'; break;
				case '\'': value = '\''; break;
				default: throw CompileError(line, string("unknown escape \\") + escape);
				}
			}
			else if (c == '\'' || c == '\n' || c == '\0')
				throw CompileError(line, "bad character literal");
			else
			{
				value = (unsigned char) c;
				position++;
			}
			if (peek() != '\'')
				throw CompileError(line, "bad character literal");
			position++;
			Token token = { Token::NUMBER, source.substr(start, position - start), value, line };
			return token;
		}

		void identifier(vector<Token>* tokens)
		{
			size_t start = position;
			while (isIdentPart(peek()))
				position++;
			Token token = { Token::IDENT, source.substr(start, position - start), 0, line };
			map<string, Macro>::const_iterator found = expandMacros ? state->macros.find(token.text) : state->macros.end();
			if (found == state->macros.end())
			{
				tokens->push_back(token);
				return;
			}
			vector<Token> call(1, token);
			if (found->second.function)
			{
				// a call has its "(" after the name, possibly on a later line
				size_t at = position;
				int callLine = line;
				while (at < source.size() && isspace((unsigned char) source[at]))
				{
					if (source[at] == '\n')
						callLine++;
					at++;
				}
				if (at >= source.size() || source[at] != '(')
				{
					tokens->push_back(token);
					return;
				}
				size_t close = at;
				int nesting = 0;
				int closeLine = callLine;
				for (; close < source.size(); close++)
				{
					char c = source[close];
					if (c == '\n')
						closeLine++;
					else if (c == '"' || c == '\'')
					{
						for (close++; close < source.size() && source[close] != c && source[close] != '\n'; close++)
						{
							if (source[close] == '\\')
								close++;
						}
					}
					else if (c == '(')
						nesting++;
					else if (c == ')' && --nesting == 0)
						break;
				}
				if (close >= source.size())
					throw CompileError(token.line, "the call of the macro " + token.text + " is not closed");
				vector<Token> arguments = tokensOf(source.substr(at, close - at + 1), callLine);
				call.insert(call.end(), arguments.begin(), arguments.end());
				position = close + 1;
				line = closeLine;
			}
			set<string> noneActive;
			vector<Token> expanded = expandTokens(call, &noneActive);
			tokens->insert(tokens->end(), expanded.begin(), expanded.end());
		}

		Token operatorToken()
		{
			for (size_t i = 0; i < sizeof(OPERATORS) / sizeof(OPERATORS[0]); i++)
			{
				size_t length = strlen(OPERATORS[i]);
				if (source.compare(position, length, OPERATORS[i]) == 0)
				{
					position += length;
					Token token = { Token::OPERATOR, OPERATORS[i], 0, line };
					return token;
				}
			}
			throw CompileError(line, string("unexpected character '") + source[position] + "'");
		}
	};
}

vector<Token> tokenize(const string& source, const string& includeDir, LexInfo* info)
{
	State state;
	vector<Token> tokens = Lexer(source, includeDir, &state).run();
	if (info != NULL)
	{
		info->includes = state.includes;
		info->macros.clear();
		for (map<string, Macro>::const_iterator m = state.macros.begin(); m != state.macros.end(); ++m)
			info->macros[m->first] = m->second.body;
	}
	return tokens;
}

vector<Token> tokenize(const string& source)
{
	return tokenize(source, "", NULL);
}
