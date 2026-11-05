#ifndef LEXER_HPP
#define LEXER_HPP
#include <string>
#include <vector>
#include <unordered_map>

enum class TokenType {
    KEYWORD, IDENTIFIER, INTEGER_LITERAL, FLOAT_LITERAL, STRING_LITERAL,
    OPERATOR, PUNCTUATOR, CHAR_LITERAL ,UNKNOWN
};

struct Token {
    TokenType type;
    std::string value;
    int line, col;
    Token(TokenType t, const std::string& v, int l = 0, int c = 0)
        : type(t), value(v), line(l), col(c) {}
};

class Lexer {
private:
    std::string input;
    size_t position = 0;
    int line = 1, col = 1;
    std::unordered_map<std::string, TokenType> keywords;

    void initKeywords();
    static bool isWhitespace(char c);
    static bool isAlpha(char c);
    static bool isDigit(char c);
    static bool isAlphaNumeric(char c);
    char peek(int off = 0) const;
    void advance();
    std::string getNextWord();
    std::string getNextNumber();
    std::string getNextString();

public:
    explicit Lexer(const std::string& source);
    std::vector<Token> tokenize();
};

std::string tokenTypeToString(TokenType type);
#endif
