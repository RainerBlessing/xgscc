#include "lexer.h"
#include <cctype>
#include <cstring>
#include "errors.h"

using namespace std;

namespace
{
	// longest first, so that "<<=" is not read as "<<" and "="
	const char* const OPERATORS[] = {
		"<<=", ">>=",
		"++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
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
				else if (isIdentStart(c))
					tokens.push_back(identifier());
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

		// Only #include is accepted, and ignored: stdint.h is built in.
		void directive()
		{
			position++;
			while (peek() == ' ' || peek() == '\t')
				position++;
			size_t nameStart = position;
			while (isIdentPart(peek()))
				position++;
			string name = source.substr(nameStart, position - nameStart);
			if (name != "include")
				throw CompileError(line, "#" + name + " is not supported yet");
			while (position < source.size() && source[position] != '\n')
				position++;
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

		Token identifier()
		{
			size_t start = position;
			while (isIdentPart(peek()))
				position++;
			Token token = { Token::IDENT, source.substr(start, position - start), 0, line };
			return token;
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
