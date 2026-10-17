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
	default:
		return "(" + expr.name + " " + toString(*expr.left) + " " + toString(*expr.right) + ")";
	}
}
