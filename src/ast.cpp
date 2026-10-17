#include "ast.h"
#include <sstream>

using namespace std;

string toString(const Expr& expr)
{
	switch (expr.kind)
	{
	case Expr::NUMBER:
	{
		ostringstream out;
		out << expr.value;
		return out.str();
	}
	case Expr::VARIABLE:
		return expr.name;
	case Expr::UNARY:
		return "(" + expr.name + " " + toString(*expr.left) + ")";
	case Expr::CALL:
	{
		string text = "(call " + expr.name;
		for (size_t i = 0; i < expr.arguments.size(); i++)
			text += " " + toString(*expr.arguments[i]);
		return text + ")";
	}
	default:
		return "(" + expr.name + " " + toString(*expr.left) + " " + toString(*expr.right) + ")";
	}
}
