#include "lexer.h"
#include <cctype>
#include <cstring>
#include <map>
#include <set>
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

	class Lexer
	{
	public:
		Lexer(const string& source) : source(source), position(0), line(1), lineStart(true) {}

		vector<Token> run()
		{
			vector<Token> tokens;
			for (;;)
			{
				skipSpaceAndComments();
				if (position >= source.size())
					break;
				char c = source[position];
				if (c == '#' && lineStart)
					directive();
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
			Token end = { Token::END, "", 0, line };
			tokens.push_back(end);
			return tokens;
		}

	private:
		const string& source;
		size_t position;
		int line;
		bool lineStart;		// only blanks since the last line break
		map<string, vector<Token> > macros;	// #define NAME tokens

		char peek(size_t offset = 0) const
		{
			return position + offset < source.size() ? source[position + offset] : '\0';
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

		// #include is ignored (stdint.h is built in), #define and #undef work for
		// names without parameters.
		void directive()
		{
			position++;
			while (peek() == ' ' || peek() == '\t')
				position++;
			size_t nameStart = position;
			while (isIdentPart(peek()))
				position++;
			string name = source.substr(nameStart, position - nameStart);
			string rest = restOfLine();
			if (name == "include")
				return;
			if (name == "define")
				define(rest);
			else if (name == "undef")
				macros.erase(macroName(rest));
			else
				throw CompileError(line, "#" + name + " is not supported yet");
		}

		string restOfLine()
		{
			size_t start = position;
			while (position < source.size() && source[position] != '\n')
				position++;
			return source.substr(start, position - start);
		}

		// the name at the start of a directive's text
		string macroName(const string& text) const
		{
			size_t first = text.find_first_not_of(" \t");
			if (first == string::npos || !isIdentStart(text[first]))
				throw CompileError(line, "a name is missing after #define or #undef");
			size_t end = first;
			while (end < text.size() && isIdentPart(text[end]))
				end++;
			return text.substr(first, end - first);
		}

		void define(const string& text)
		{
			string name = macroName(text);
			size_t afterName = text.find(name) + name.size();
			if (afterName < text.size() && text[afterName] == '(')
				throw CompileError(line, "macros with parameters are not supported yet");
			string replacement = text.substr(afterName);
			Lexer inner(replacement);
			vector<Token> tokens = inner.run();
			tokens.pop_back();		// END
			macros[name] = tokens;
		}

		void expand(const string& name, int useLine, set<string>* active, vector<Token>* out)
		{
			active->insert(name);
			const vector<Token>& replacement = macros[name];
			for (size_t i = 0; i < replacement.size(); i++)
			{
				Token token = replacement[i];
				token.line = useLine;
				if (token.type == Token::IDENT && macros.count(token.text) && !active->count(token.text))
					expand(token.text, useLine, active, out);
				else
					out->push_back(token);
			}
			active->erase(name);
		}

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
			if (macros.count(token.text))
			{
				set<string> active;
				expand(token.text, line, &active, tokens);
			}
			else
				tokens->push_back(token);
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

vector<Token> tokenize(const string& source)
{
	return Lexer(source).run();
}
