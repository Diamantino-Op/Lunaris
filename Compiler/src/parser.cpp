#include "parser.h"

#include <utility>

namespace lunaris {

Parser::Parser(std::vector<Token> tokens, DiagnosticSink& diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

const Token& Parser::current() const {
    return tokens_[index_];
}

const Token& Parser::previous() const {
    return tokens_[index_ - 1];
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    ++index_;
    return true;
}

bool Parser::check(TokenKind kind) const {
    return current().kind == kind;
}

const Token& Parser::consume(TokenKind kind, std::string message) {
    if (check(kind)) {
        ++index_;
        return previous();
    }
    diagnostics_.error(current().location, std::move(message));
    return current();
}

Token Parser::consume_identifier(std::string message) {
    if (check(TokenKind::Identifier)) {
        ++index_;
        return previous();
    }
    diagnostics_.error(current().location, std::move(message));
    return current();
}

void Parser::synchronize() {
    if (index_ == 0) {
        ++index_;
        return;
    }

    while (!check(TokenKind::EndOfFile)) {
        TokenKind previous_kind = previous().kind;
        if (previous_kind == TokenKind::Semicolon || previous_kind == TokenKind::KeywordEnd || previous_kind == TokenKind::RBrace) {
            return;
        }
        ++index_;
    }
}

TypeRef Parser::parse_type() {
    Token name = consume_identifier("expected type name");
    TypeRef type = TypeRef::named(name.lexeme);
    while (match(TokenKind::Star)) {
        type = TypeRef::pointer(std::move(type));
    }
    return type;
}

std::shared_ptr<Expression> Parser::parse_primary_expression() {
    const Token& token = current();
    if (match(TokenKind::Identifier)) {
        auto expression = std::make_shared<Expression>();
        expression->kind = ExpressionKind::Identifier;
        expression->location = token.location;
        expression->text = token.lexeme;
        return expression;
    }
    if (match(TokenKind::Number)) {
        auto expression = std::make_shared<Expression>();
        expression->kind = ExpressionKind::Number;
        expression->location = token.location;
        expression->text = token.lexeme;
        return expression;
    }
    if (match(TokenKind::String)) {
        auto expression = std::make_shared<Expression>();
        expression->kind = ExpressionKind::String;
        expression->location = token.location;
        expression->text = token.lexeme;
        return expression;
    }
    if (match(TokenKind::KeywordTrue) || match(TokenKind::KeywordFalse) || match(TokenKind::KeywordNil)) {
        auto expression = std::make_shared<Expression>();
        expression->kind = ExpressionKind::Identifier;
        expression->location = token.location;
        expression->text = token.lexeme;
        return expression;
    }
    if (match(TokenKind::LParen)) {
        auto expression = parse_expression();
        consume(TokenKind::RParen, "expected ')' after expression");
        return expression;
    }

    diagnostics_.error(token.location, "expected expression");
    synchronize();
    auto expression = std::make_shared<Expression>();
    expression->location = token.location;
    expression->kind = ExpressionKind::Identifier;
    expression->text = "<error>";
    return expression;
}

std::shared_ptr<Expression> Parser::parse_postfix_expression(std::shared_ptr<Expression> expression) {
    for (;;) {
        if (match(TokenKind::LParen)) {
            auto call = std::make_shared<Expression>();
            call->kind = ExpressionKind::Call;
            call->location = expression->location;
            call->callee = std::move(expression);
            if (!check(TokenKind::RParen)) {
                while (true) {
                    call->arguments.push_back(*parse_expression());
                    if (!match(TokenKind::Comma)) {
                        break;
                    }
                }
            }
            consume(TokenKind::RParen, "expected ')' after arguments");
            expression = std::move(call);
            continue;
        }
        if (match(TokenKind::Dot)) {
            Token member = consume_identifier("expected field name after '.'");
            auto access = std::make_shared<Expression>();
            access->kind = ExpressionKind::Member;
            access->location = expression->location;
            access->object = std::move(expression);
            access->text = member.lexeme;
            expression = std::move(access);
            continue;
        }
        if (match(TokenKind::LBracket)) {
            auto index = std::make_shared<Expression>();
            index->kind = ExpressionKind::Index;
            index->location = expression->location;
            index->object = std::move(expression);
            index->right = parse_expression();
            consume(TokenKind::RBracket, "expected ']' after index expression");
            expression = std::move(index);
            continue;
        }
        break;
    }
    return expression;
}

std::shared_ptr<Expression> Parser::parse_prefix_expression() {
    if (match(TokenKind::Minus) || match(TokenKind::Star) || match(TokenKind::Ampersand)) {
        const Token& operator_token = previous();
        auto expression = std::make_shared<Expression>();
        expression->kind = ExpressionKind::Unary;
        expression->location = operator_token.location;
        expression->op = operator_token.lexeme;
        expression->right = parse_expression(4);
        return expression;
    }

    return parse_postfix_expression(parse_primary_expression());
}

int Parser::precedence_of(TokenKind kind) const {
    switch (kind) {
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
        return 1;
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual:
        return 2;
    case TokenKind::Star:
    case TokenKind::Slash:
        return 4;
    case TokenKind::Plus:
    case TokenKind::Minus:
        return 3;
    default:
        return 0;
    }
}

std::shared_ptr<Expression> Parser::parse_expression(int precedence) {
    auto left = parse_prefix_expression();

    while (true) {
        int current_precedence = precedence_of(current().kind);
        if (current_precedence <= precedence) {
            break;
        }

        Token operator_token = current();
        ++index_;
        auto right = parse_expression(current_precedence);
        auto expression = std::make_shared<Expression>();
        expression->kind = ExpressionKind::Binary;
        expression->location = operator_token.location;
        expression->op = operator_token.lexeme;
        expression->left = std::move(left);
        expression->right = std::move(right);
        left = std::move(expression);
    }

    return left;
}

std::vector<Statement> Parser::parse_block() {
    std::vector<Statement> body;
    while (!check(TokenKind::KeywordEnd) && !check(TokenKind::EndOfFile)) {
        body.push_back(parse_statement());
    }
    return body;
}

Statement Parser::parse_statement() {
    const Token& token = current();
    if (match(TokenKind::KeywordLocal)) {
        Token name = consume_identifier("expected local variable name");
        Statement statement;
        statement.kind = StatementKind::Local;
        statement.location = token.location;
        statement.name = name.lexeme;
        if (match(TokenKind::Colon)) {
            statement.type = parse_type();
        }
        if (match(TokenKind::Equal)) {
            statement.value = parse_expression();
        }
        match(TokenKind::Semicolon);
        return statement;
    }

    if (match(TokenKind::KeywordIf)) {
        Statement statement;
        statement.kind = StatementKind::If;
        statement.location = token.location;
        statement.condition = parse_expression();
        consume(TokenKind::KeywordThen, "expected 'then' after if condition");
        statement.body = parse_block();
        consume(TokenKind::KeywordEnd, "expected 'end' to close if block");
        return statement;
    }

    if (match(TokenKind::KeywordWhile)) {
        Statement statement;
        statement.kind = StatementKind::While;
        statement.location = token.location;
        statement.condition = parse_expression();
        consume(TokenKind::KeywordDo, "expected 'do' after while condition");
        statement.body = parse_block();
        consume(TokenKind::KeywordEnd, "expected 'end' to close while block");
        return statement;
    }

    if (match(TokenKind::KeywordReturn)) {
        Statement statement;
        statement.kind = StatementKind::Return;
        statement.location = token.location;
        if (!check(TokenKind::Semicolon) && !check(TokenKind::KeywordEnd) && !check(TokenKind::EndOfFile)) {
            statement.expression = parse_expression();
        }
        match(TokenKind::Semicolon);
        return statement;
    }

    auto expression = parse_expression();
    if (match(TokenKind::Equal)) {
        Statement statement;
        statement.kind = StatementKind::Assignment;
        statement.location = token.location;
        statement.target = std::move(expression);
        statement.value = parse_expression();
        match(TokenKind::Semicolon);
        return statement;
    }

    Statement statement;
    statement.kind = StatementKind::Expression;
    statement.location = token.location;
    statement.expression = std::move(expression);
    match(TokenKind::Semicolon);
    return statement;
}

StructDecl Parser::parse_struct_decl(bool packed) {
    StructDecl declaration;
    declaration.packed = packed;
    declaration.location = previous().location;
    declaration.name = consume_identifier("expected struct name").lexeme;
    consume(TokenKind::LBrace, "expected '{' after struct name");

    while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
        Token field_name = consume_identifier("expected field name");
        consume(TokenKind::Colon, "expected ':' after field name");
        FieldDecl field;
        field.location = field_name.location;
        field.name = field_name.lexeme;
        field.type = parse_type();
        declaration.fields.push_back(std::move(field));
        match(TokenKind::Semicolon);
    }

    consume(TokenKind::RBrace, "expected '}' after struct body");
    return declaration;
}

FunctionDecl Parser::parse_function_decl(bool external_asm) {
    FunctionDecl declaration;
    declaration.external_asm = external_asm;
    declaration.location = previous().location;
    declaration.name = consume_identifier("expected function name").lexeme;
    consume(TokenKind::LParen, "expected '(' after function name");

    if (!check(TokenKind::RParen)) {
        while (true) {
            Token parameter_name = consume_identifier("expected parameter name");
            consume(TokenKind::Colon, "expected ':' after parameter name");
            ParameterDecl parameter;
            parameter.location = parameter_name.location;
            parameter.name = parameter_name.lexeme;
            parameter.type = parse_type();
            declaration.parameters.push_back(std::move(parameter));
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
    }

    consume(TokenKind::RParen, "expected ')' after parameters");
    if (match(TokenKind::Colon)) {
        declaration.return_type = parse_type();
    }

    if (external_asm) {
        if (match(TokenKind::Equal)) {
            if (check(TokenKind::Identifier) || check(TokenKind::String)) {
                declaration.asm_symbol = current().lexeme;
                ++index_;
            } else {
                diagnostics_.error(current().location, "expected asm symbol name");
            }
        }
        match(TokenKind::Semicolon);
        return declaration;
    }

    while (!check(TokenKind::KeywordEnd) && !check(TokenKind::EndOfFile)) {
        declaration.body.push_back(parse_statement());
    }

    consume(TokenKind::KeywordEnd, "expected 'end' to close function");
    return declaration;
}

ParseResult Parser::parse() {
    while (!check(TokenKind::EndOfFile)) {
        if (match(TokenKind::KeywordPacked)) {
            consume(TokenKind::KeywordStruct, "expected 'struct' after 'packed'");
            program_.structs.push_back(parse_struct_decl(true));
            continue;
        }

        if (match(TokenKind::KeywordStruct)) {
            program_.structs.push_back(parse_struct_decl(false));
            continue;
        }

        if (match(TokenKind::KeywordAsm)) {
            consume(TokenKind::KeywordFunction, "expected 'function' after 'asm'");
            program_.functions.push_back(parse_function_decl(true));
            continue;
        }

        if (match(TokenKind::KeywordFunction)) {
            program_.functions.push_back(parse_function_decl(false));
            continue;
        }

        diagnostics_.error(current().location, "unexpected top-level token");
        synchronize();
    }

    ParseResult result;
    result.ok = !diagnostics_.has_errors();
    result.program = std::move(program_);
    result.diagnostics = diagnostics_.diagnostics();
    return result;
}

} // namespace lunaris
