#include "lexer.h"

#include <cctype>

namespace lunaris {

Lexer::Lexer(std::string_view source, DiagnosticSink& diagnostics)
    : source_(source), diagnostics_(diagnostics) {}

char Lexer::peek(std::size_t offset) const {
    if (index_ + offset >= source_.size()) {
        return '\0';
    }
    return source_[index_ + offset];
}

char Lexer::advance() {
    if (index_ >= source_.size()) {
        return '\0';
    }
    char current = source_[index_++];
    if (current == '\n') {
        ++location_.line;
        location_.column = 1;
    } else {
        ++location_.column;
    }
    return current;
}

void Lexer::skip_whitespace_and_comments() {
    for (;;) {
        char current = peek();
        if (current == '\0') {
            return;
        }
        if (std::isspace(static_cast<unsigned char>(current))) {
            advance();
            continue;
        }
        if (current == '-' && peek(1) == '-') {
            while (peek() != '\0' && peek() != '\n') {
                advance();
            }
            continue;
        }
        return;
    }
}

Token Lexer::make_token(TokenKind kind, std::string lexeme, SourceLocation location) {
    return Token{kind, std::move(lexeme), location};
}

Token Lexer::lex_identifier_or_keyword() {
    SourceLocation start = location_;
    std::string lexeme;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
        lexeme.push_back(advance());
    }

    TokenKind kind = TokenKind::Identifier;
    if (lexeme == "function") kind = TokenKind::KeywordFunction;
    else if (lexeme == "local") kind = TokenKind::KeywordLocal;
    else if (lexeme == "return") kind = TokenKind::KeywordReturn;
    else if (lexeme == "struct") kind = TokenKind::KeywordStruct;
    else if (lexeme == "asm") kind = TokenKind::KeywordAsm;
    else if (lexeme == "packed") kind = TokenKind::KeywordPacked;
    else if (lexeme == "data") kind = TokenKind::KeywordData;
    else if (lexeme == "section") kind = TokenKind::KeywordSection;
    else if (lexeme == "end") kind = TokenKind::KeywordEnd;
    else if (lexeme == "extern") kind = TokenKind::KeywordExtern;
    else if (lexeme == "if") kind = TokenKind::KeywordIf;
    else if (lexeme == "then") kind = TokenKind::KeywordThen;
    else if (lexeme == "while") kind = TokenKind::KeywordWhile;
    else if (lexeme == "do") kind = TokenKind::KeywordDo;
    else if (lexeme == "true") kind = TokenKind::KeywordTrue;
    else if (lexeme == "false") kind = TokenKind::KeywordFalse;
    else if (lexeme == "nil") kind = TokenKind::KeywordNil;

    return Token{kind, std::move(lexeme), start};
}

Token Lexer::lex_number() {
    SourceLocation start = location_;
    std::string lexeme;
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        lexeme.push_back(advance());
        lexeme.push_back(advance());
        while (std::isxdigit(static_cast<unsigned char>(peek()))) {
            lexeme.push_back(advance());
        }
    } else {
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            lexeme.push_back(advance());
        }
    }
    return Token{TokenKind::Number, std::move(lexeme), start};
}

Token Lexer::lex_string() {
    SourceLocation start = location_;
    std::string lexeme;
    advance();
    while (peek() != '\0' && peek() != '"') {
        if (peek() == '\n') {
            diagnostics_.error(start, "unterminated string literal");
            break;
        }
        lexeme.push_back(advance());
    }
    if (peek() == '"') {
        advance();
    }
    return Token{TokenKind::String, std::move(lexeme), start};
}

std::vector<Token> Lexer::lex() {
    std::vector<Token> tokens;
    while (true) {
        skip_whitespace_and_comments();
        SourceLocation start = location_;
        char current = peek();
        if (current == '\0') {
            tokens.push_back(Token{TokenKind::EndOfFile, "", start});
            break;
        }
        if (std::isalpha(static_cast<unsigned char>(current)) || current == '_') {
            tokens.push_back(lex_identifier_or_keyword());
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(current))) {
            tokens.push_back(lex_number());
            continue;
        }
        switch (current) {
        case '"': tokens.push_back(lex_string()); break;
        case '(': tokens.push_back(make_token(TokenKind::LParen, std::string(1, advance()), start)); break;
        case ')': tokens.push_back(make_token(TokenKind::RParen, std::string(1, advance()), start)); break;
        case '{': tokens.push_back(make_token(TokenKind::LBrace, std::string(1, advance()), start)); break;
        case '}': tokens.push_back(make_token(TokenKind::RBrace, std::string(1, advance()), start)); break;
        case '[': tokens.push_back(make_token(TokenKind::LBracket, std::string(1, advance()), start)); break;
        case ']': tokens.push_back(make_token(TokenKind::RBracket, std::string(1, advance()), start)); break;
        case ',': tokens.push_back(make_token(TokenKind::Comma, std::string(1, advance()), start)); break;
        case '.': tokens.push_back(make_token(TokenKind::Dot, std::string(1, advance()), start)); break;
        case ':': tokens.push_back(make_token(TokenKind::Colon, std::string(1, advance()), start)); break;
        case ';': tokens.push_back(make_token(TokenKind::Semicolon, std::string(1, advance()), start)); break;
        case '=':
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back(make_token(TokenKind::EqualEqual, "==", start));
            } else {
                tokens.push_back(make_token(TokenKind::Equal, "=", start));
            }
            break;
        case '!':
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back(make_token(TokenKind::BangEqual, "!=", start));
            } else {
                diagnostics_.error(start, "unexpected character: !");
                tokens.push_back(make_token(TokenKind::Unknown, "!", start));
            }
            break;
        case '~':
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back(make_token(TokenKind::TildeEqual, "~=", start));
            } else {
                diagnostics_.error(start, "unexpected character: ~");
                tokens.push_back(make_token(TokenKind::Unknown, "~", start));
            }
            break;
        case '<':
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back(make_token(TokenKind::LessEqual, "<=", start));
            } else {
                tokens.push_back(make_token(TokenKind::Less, "<", start));
            }
            break;
        case '>':
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back(make_token(TokenKind::GreaterEqual, ">=", start));
            } else {
                tokens.push_back(make_token(TokenKind::Greater, ">", start));
            }
            break;
        case '+': tokens.push_back(make_token(TokenKind::Plus, std::string(1, advance()), start)); break;
        case '-': tokens.push_back(make_token(TokenKind::Minus, std::string(1, advance()), start)); break;
        case '*': tokens.push_back(make_token(TokenKind::Star, std::string(1, advance()), start)); break;
        case '/': tokens.push_back(make_token(TokenKind::Slash, std::string(1, advance()), start)); break;
        case '&': tokens.push_back(make_token(TokenKind::Ampersand, std::string(1, advance()), start)); break;
        default:
            diagnostics_.error(start, std::string("unexpected character: ") + current);
            tokens.push_back(make_token(TokenKind::Unknown, std::string(1, advance()), start));
            break;
        }
    }
    return tokens;
}

} // namespace lunaris
