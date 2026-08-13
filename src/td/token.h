#pragma once
#include <cstdint>

namespace td {

enum class TokenType : uint8_t {
    // Literals
    Integer,
    Float,
    String,
    Identifier,
    
    // Keywords
    Let,
    Const,
    Fn,
    If,
    Else,
    While,
    For,
    Return,
    True,
    False,
    Null,
    Struct,
    Entity,
    This,
    Break,
    Continue,
    
    // Types
    IntType,
    FloatType,
    StringType,
    BoolType,
    VoidType,
    
    // Operators
    Plus,           // +
    Minus,          // -
    Star,           // *
    Slash,          // /
    Percent,        // %
    
    Equals,         // =
    EqualsEquals,   // ==
    BangEquals,     // !=
    Less,           // <
    LessEqual,      // <=
    Greater,        // >
    GreaterEqual,   // >=
    
    And,            // &&
    Or,             // ||
    Bang,           // !
    
    PlusEquals,     // +=
    MinusEquals,    // -=
    StarEquals,     // *=
    SlashEquals,    // /=
    
    PlusPlus,       // ++
    MinusMinus,     // --
    
    Arrow,          // ->
    
    // Punctuation
    Dot,            // .
    Comma,          // ,
    Colon,          // :
    Semicolon,      // ;
    
    LeftParen,      // (
    RightParen,     // )
    LeftBrace,      // {
    RightBrace,     // }
    LeftBracket,    // [
    RightBracket,   // ]
    
    // Special
    EndOfFile,
    Error,
    Newline
};

struct Token {
    TokenType type = TokenType::Error;
    int line = 0;
    int column = 0;
    char lexeme[128] = {};
    
    union {
        int64_t intValue;
        double floatValue;
    };
};

const char* tokenTypeName(TokenType type);

} // namespace td
