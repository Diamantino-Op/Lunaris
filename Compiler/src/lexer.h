#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "diagnostic.h"
#include "token.h"

namespace lunaris {

class Lexer {
public:
    Lexer(std::string_view source, DiagnosticSink& diagnostics);

    [[nodiscard]] std::vector<Token> lex();

private:
    char peek(std::size_t offset = 0) const;
    char advance();
    void skip_whitespace_and_comments();
    Token lex_identifier_or_keyword();
    Token lex_number();
    Token lex_string();
    Token make_token(TokenKind kind, std::string lexeme, SourceLocation location);

    std::string_view source_;
    DiagnosticSink& diagnostics_;
    std::size_t index_ = 0;
    SourceLocation location_{};
};

} // namespace lunaris
