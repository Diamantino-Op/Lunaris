#pragma once

#include <string>
#include <string_view>

namespace lunaris {

enum class TokenKind {
    EndOfFile,
    Identifier,
    Number,
    String,
    KeywordFunction,
    KeywordLocal,
    KeywordReturn,
    KeywordStruct,
    KeywordAsm,
    KeywordPacked,
    KeywordEnd,
    KeywordExtern,
    KeywordTrue,
    KeywordFalse,
    KeywordNil,
    KeywordIf,
    KeywordThen,
    KeywordWhile,
    KeywordDo,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Dot,
    Colon,
    Semicolon,
    Equal,
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Plus,
    Minus,
    Star,
    Slash,
    Ampersand,
    Unknown,
};

struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

struct Token {
    TokenKind kind = TokenKind::Unknown;
    std::string lexeme;
    SourceLocation location;
};

std::string_view to_string(TokenKind kind);

} // namespace lunaris
