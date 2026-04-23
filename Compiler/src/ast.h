#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "token.h"

namespace lunaris {

enum class TypeKind {
    Named,
    Pointer,
};

struct TypeRef {
    TypeKind kind = TypeKind::Named;
    std::string name;
    std::shared_ptr<TypeRef> element;

    static TypeRef named(std::string value) {
        TypeRef type;
        type.kind = TypeKind::Named;
        type.name = std::move(value);
        return type;
    }

    static TypeRef pointer(TypeRef value) {
        TypeRef type;
        type.kind = TypeKind::Pointer;
        type.element = std::make_shared<TypeRef>(std::move(value));
        return type;
    }
};

enum class ExpressionKind {
    Identifier,
    Number,
    String,
    Unary,
    Binary,
    Call,
    Member,
    Index,
};

struct Expression {
    ExpressionKind kind = ExpressionKind::Identifier;
    SourceLocation location;
    std::string text;
    std::string op;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
    std::shared_ptr<Expression> callee;
    std::shared_ptr<Expression> object;
    std::vector<Expression> arguments;
};

enum class StatementKind {
    Local,
    Return,
    Assignment,
    Expression,
    If,
    While,
};

struct Statement {
    StatementKind kind = StatementKind::Expression;
    SourceLocation location;
    std::string name;
    std::optional<TypeRef> type;
    std::shared_ptr<Expression> expression;
    std::shared_ptr<Expression> target;
    std::shared_ptr<Expression> value;
    std::shared_ptr<Expression> condition;
    std::vector<Statement> body;
};

struct ParameterDecl {
    SourceLocation location;
    std::string name;
    TypeRef type;
};

struct FieldDecl {
    SourceLocation location;
    std::string name;
    TypeRef type;
};

struct StructDecl {
    SourceLocation location;
    bool packed = false;
    std::string name;
    std::vector<FieldDecl> fields;
};

struct FunctionDecl {
    SourceLocation location;
    bool external_asm = false;
    std::string name;
    std::string asm_symbol;
    std::vector<ParameterDecl> parameters;
    std::optional<TypeRef> return_type;
    std::vector<Statement> body;
};

struct Program {
    std::vector<StructDecl> structs;
    std::vector<FunctionDecl> functions;
};

} // namespace lunaris