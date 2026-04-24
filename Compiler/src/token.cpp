#include "token.h"

namespace lunaris {

std::string_view to_string(TokenKind kind) {
    switch (kind) {
    case TokenKind::EndOfFile: return "eof";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::Number: return "number";
    case TokenKind::String: return "string";
    case TokenKind::KeywordFunction: return "function";
    case TokenKind::KeywordLocal: return "local";
    case TokenKind::KeywordReturn: return "return";
    case TokenKind::KeywordStruct: return "struct";
    case TokenKind::KeywordAsm: return "asm";
    case TokenKind::KeywordRequire: return "require";
    case TokenKind::KeywordPacked: return "packed";
    case TokenKind::KeywordData: return "data";
    case TokenKind::KeywordEnd: return "end";
    case TokenKind::KeywordExtern: return "extern";
    case TokenKind::KeywordTrue: return "true";
    case TokenKind::KeywordFalse: return "false";
    case TokenKind::KeywordNil: return "nil";
    case TokenKind::KeywordIf: return "if";
    case TokenKind::KeywordThen: return "then";
    case TokenKind::KeywordWhile: return "while";
    case TokenKind::KeywordDo: return "do";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::LBrace: return "{";
    case TokenKind::RBrace: return "}";
    case TokenKind::LBracket: return "[";
    case TokenKind::RBracket: return "]";
    case TokenKind::Comma: return ",";
    case TokenKind::Dot: return ".";
    case TokenKind::Colon: return ":";
    case TokenKind::Semicolon: return ";";
    case TokenKind::Equal: return "=";
    case TokenKind::EqualEqual: return "==";
    case TokenKind::BangEqual: return "!=";
    case TokenKind::TildeEqual: return "~=";
    case TokenKind::Less: return "<";
    case TokenKind::LessEqual: return "<=";
    case TokenKind::Greater: return ">";
    case TokenKind::GreaterEqual: return ">=";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Ampersand: return "&";
    case TokenKind::Unknown: return "unknown";
    }
    return "unknown";
}

} // namespace lunaris
