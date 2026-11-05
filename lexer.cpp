#include "lexer.hpp"

void Lexer::initKeywords() {
    for (auto& kw : {
        "if", "else", "while", "for", "in", "loop", "block",
        "guard", "match", "defer", "return", "break", "continue",
        "print",
        "case", "default", "do", "yield",
        "try", "catch", "finally", "throw", "panic", "assert", "recover",
        "import", "namespace", "struct", "enum", "type", "fn", "thread_fn",
        "export", "pub", "static", "inline", "unsafe", "asm", "macro",
        "simd", "recursive", "let", "var", "const", "mut",
        "extern", "pad", "reusable", "as",
        "class", "trait", "impl", "extends", "super", "this",
        "ptr", "rawptr", "ref", "new", "delete", "move",
        "sizeof", "typeof",
        "async", "await", "spawn", "join", "lock", "unlock",
        "atomic", "sync",
        "true", "false", "null", "nullptr", "self", "inf", "nan",
        "bool", "char", "byte", "varint", "any", "auto",
        "usize", "isize",
        "i8",  "i16", "i32", "i64", "i128",
        "u8",  "u16", "u32", "u64", "u128",
        "f16", "f32", "f64",
        "string", "int", "float", "void"
    })
        keywords[kw] = TokenType::KEYWORD;
}

bool Lexer::isWhitespace(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'; }
bool Lexer::isAlpha(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
bool Lexer::isDigit(char c) { return c>='0'&&c<='9'; }
bool Lexer::isAlphaNumeric(char c) { return isAlpha(c)||isDigit(c); }

char Lexer::peek(int off) const {
    return (position+off < input.length()) ? input[position+off] : '\0';
}

void Lexer::advance() {
    if (position >= input.length()) return;
    if (input[position] == '\n') { line++; col = 1; }
    else col++;
    position++;
}

std::string Lexer::getNextWord() {
    std::string s;
    while (isAlphaNumeric(peek())) { s += peek(); advance(); }
    return s;
}

std::string Lexer::getNextNumber() {
    std::string s;
    bool dot = false;
    while (isDigit(peek()) || (!dot && peek()=='.')) {
        if (peek()=='.') dot = true;
        s += peek(); advance();
    }
    return s;
}

std::string Lexer::getNextString() {
    std::string s;
    advance(); 
    while (peek() != '"' && peek() != '\0') {
        if (peek() == '\\') {
            advance();
            switch (peek()) {
                case 'n': s += '\n'; break;
                case 't': s += '\t'; break;
                case 'r': s += '\r'; break;
                case '\\': s += '\\'; break;
                case '"': s += '"'; break;
                default: s += peek();
            }
            advance();
        } else { s += peek(); advance(); }
    }
    if (peek() == '"') advance();
    return s;
}

Lexer::Lexer(const std::string& src) : input(src) { initKeywords(); }

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (position < input.length()) {
        char c = peek();
        int tl = line, tc = col;

        if (isWhitespace(c)) { advance(); continue; }
        if (c == '/' && peek(1) == '/') {
            while (peek() != '\n' && peek() != '\0') advance();
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            advance(); advance();
            int depth = 1;
            while (peek() != '\0' && depth > 0) {
                if (peek()=='/' && peek(1)=='*') { depth++; advance(); advance(); }
                else if (peek()=='*' && peek(1)=='/') { depth--; advance(); advance(); }
                else advance();
            }
            continue;
        }
        if (c == '\'') {
          advance();
          long value = 0;
          if (peek() == '\\') {
            advance();
            switch (peek()) {
              case 'n': value = '\n'; break;
              case 't': value = '\t'; break;
              case 'r': value = '\r'; break;
              case '\\': value = '\\'; break;
              case '\'': value = '\''; break;
              case '0': value = '\0'; break;
              default: value = (unsigned char)peek();
          }
          advance();
        } else if (peek() != '\'' && peek() != '\0') {
          value = (unsigned char)peek();
          advance();
        } else {
          tokens.emplace_back(TokenType::UNKNOWN, "empty char literal", tl, tc);
          continue;
        }
        if (peek() == '\'') advance();
          tokens.emplace_back(TokenType::CHAR_LITERAL, std::to_string(value), tl, tc);
          continue;
        }
        if (c == '"') {
            tokens.emplace_back(TokenType::STRING_LITERAL, getNextString(), tl, tc);
            continue;
        }
        if (isAlpha(c)) {
            std::string w = getNextWord();
            auto it = keywords.find(w);
            tokens.emplace_back(it != keywords.end() ? it->second : TokenType::IDENTIFIER, w, tl, tc);
            continue;
        }
        if (isDigit(c)) {
            std::string n = getNextNumber();
            tokens.emplace_back(n.find('.') != std::string::npos
                ? TokenType::FLOAT_LITERAL : TokenType::INTEGER_LITERAL, n, tl, tc);
            continue;
        }
        auto two = [&](char a, char b, const char* op) {
            if (c==a && peek(1)==b) {
                advance(); advance();
                tokens.emplace_back(TokenType::OPERATOR, op, tl, tc);
                return true;
            }
            return false;
        };
        if (two('=','=',"==")||two('!','=',"!=")||two('<','=',"<=")||
            two('>','=',">=")||two('&','&',"&&")||two('|','|',"||")) continue;

        if (c=='+'||c=='-'||c=='*'||c=='/'||c=='='||c=='!'||c=='<'||c=='>') {
            advance();
            tokens.emplace_back(TokenType::OPERATOR, std::string(1,c), tl, tc);
            continue;
        }

        if (c=='('||c==')'||c=='{'||c=='}'||c==';'||c==','||c=='['||c==']') {
            advance();
            tokens.emplace_back(TokenType::PUNCTUATOR, std::string(1,c), tl, tc);
            continue;
        }
        advance();
        tokens.emplace_back(TokenType::UNKNOWN, std::string(1,c), tl, tc);
    }
    return tokens;
}

std::string tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::KEYWORD: return "KEYWORD";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::INTEGER_LITERAL: return "INTEGER_LITERAL";
        case TokenType::FLOAT_LITERAL: return "FLOAT_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::OPERATOR: return "OPERATOR";
        case TokenType::PUNCTUATOR: return "PUNCTUATOR";
        case TokenType::CHAR_LITERAL: return "CHAR_LITERAL";
        case TokenType::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}
