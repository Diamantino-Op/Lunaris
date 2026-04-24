#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ast.h"
#include "diagnostic.h"
#include "token.h"

namespace lunaris {

struct ParseResult {
    bool ok = false;
    Program program;
    std::vector<Diagnostic> diagnostics;
};

class Parser {
public:
    Parser(std::vector<Token> tokens, DiagnosticSink& diagnostics);

    [[nodiscard]] ParseResult parse();

private:
    StructDecl parse_struct_decl(bool packed);
    DataDecl parse_data_decl();
    RequireDecl parse_require_decl();
    FunctionDecl parse_function_decl(bool external_asm);
    std::vector<Statement> parse_block();
    Statement parse_statement();
    TypeRef parse_type();
    std::shared_ptr<Expression> parse_expression(int precedence = 0);
    std::shared_ptr<Expression> parse_prefix_expression();
    std::shared_ptr<Expression> parse_primary_expression();
    std::shared_ptr<Expression> parse_postfix_expression(std::shared_ptr<Expression> expression);
    int precedence_of(TokenKind kind) const;
    void synchronize();

    const Token& current() const;
    const Token& previous() const;
    bool match(TokenKind kind);
    bool check(TokenKind kind) const;
    const Token& consume(TokenKind kind, std::string message);
    Token consume_identifier(std::string message);

    std::vector<Token> tokens_;
    DiagnosticSink& diagnostics_;
    std::size_t index_ = 0;
    Program program_;
};

} // namespace lunaris
