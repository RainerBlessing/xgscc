#include "ast.h"
#include <sstream>

using namespace std;

Type pointerTo(const Type& element)
{
	Type pointer(1, false);
	pointer.isPointer = true;
	pointer.pointeeSize = element.size;
	pointer.pointeeSigned = element.isSigned;
	return pointer;
}

string typeName(const Type& type)
{
	if (type.isPointer)
		return typeName(type.pointee()) + " *";
	return string(type.isSigned ? "" : "u") + "int" + (type.size == 1 ? "8" : "16") + "_t";
}

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
	case Expr::INDEX:
		return "(index " + toString(*expr.left) + " " + toString(*expr.right) + ")";
	case Expr::CAST:
		return "(cast " + typeName(expr.type) + " " + toString(*expr.left) + ")";
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
