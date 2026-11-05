#ifndef PARSER_HPP
#define PARSER_HPP
#include "lexer.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct Expr; struct Stmt; struct BlockStmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using BlockPtr = std::unique_ptr<BlockStmt>;

struct Expr {
    enum class Kind { IntLit, FloatLit, StringLit, Var, Binary, Unary,
                      Assign, AssignIndex, Call, Index } kind;
    int line = 0, col = 0;
    virtual ~Expr() = default;
};

struct IntLit : Expr { long value; explicit IntLit(long v):value(v){kind=Kind::IntLit;} };
struct FloatLit : Expr { double value; explicit FloatLit(double v):value(v){kind=Kind::FloatLit;} };
struct StringLit : Expr { std::string value; explicit StringLit(std::string v):value(std::move(v)){kind=Kind::StringLit;} };
struct VarExpr : Expr { std::string name; explicit VarExpr(std::string n):name(std::move(n)){kind=Kind::Var;} };

struct BinaryExpr : Expr {
    std::string op; ExprPtr left, right;
    BinaryExpr(std::string o, ExprPtr l, ExprPtr r)
        :op(std::move(o)),left(std::move(l)),right(std::move(r)){kind=Kind::Binary;}
};
struct UnaryExpr : Expr {
    std::string op; ExprPtr operand;
    UnaryExpr(std::string o, ExprPtr e):op(std::move(o)),operand(std::move(e)){kind=Kind::Unary;}
};
struct AssignExpr : Expr {
    std::string name; ExprPtr value;
    AssignExpr(std::string n, ExprPtr v):name(std::move(n)),value(std::move(v)){kind=Kind::Assign;}
};
struct AssignIndexExpr : Expr {
    std::string name; ExprPtr index, value;
    AssignIndexExpr(std::string n, ExprPtr i, ExprPtr v)
        :name(std::move(n)),index(std::move(i)),value(std::move(v)){kind=Kind::AssignIndex;}
};
struct CallExpr : Expr {
    std::string name; std::vector<ExprPtr> args;
    explicit CallExpr(std::string n):name(std::move(n)){kind=Kind::Call;}
};
struct IndexExpr : Expr {
    std::string name; ExprPtr index;
    IndexExpr(std::string n, ExprPtr i):name(std::move(n)),index(std::move(i)){kind=Kind::Index;}
};

struct Stmt {
    enum class Kind { VarDecl, ExprStmt, Block, If, While, For, Return, Print,
                      Break, Continue, FuncDecl,
                      Loop, DoWhile, Assert } kind;
    int line = 0, col = 0;
    virtual ~Stmt() = default;
};

struct BlockStmt : Stmt {
    std::vector<StmtPtr> stmts;
    BlockStmt(){kind=Kind::Block;}
};
struct VarDeclStmt : Stmt {
    std::string type, name;
    ExprPtr init;
    ExprPtr arraySize;
    std::vector<ExprPtr> arrayInit;
    VarDeclStmt(std::string t, std::string n, ExprPtr i)
        :type(std::move(t)),name(std::move(n)),init(std::move(i)){kind=Kind::VarDecl;}
};
struct ExprStmt : Stmt { ExprPtr expr; explicit ExprStmt(ExprPtr e):expr(std::move(e)){kind=Kind::ExprStmt;} };
struct IfStmt : Stmt {
    ExprPtr cond; StmtPtr thenBranch, elseBranch;
    IfStmt(ExprPtr c, StmtPtr t, StmtPtr e)
        :cond(std::move(c)),thenBranch(std::move(t)),elseBranch(std::move(e)){kind=Kind::If;}
};
struct WhileStmt : Stmt {
    ExprPtr cond; StmtPtr body;
    WhileStmt(ExprPtr c, StmtPtr b):cond(std::move(c)),body(std::move(b)){kind=Kind::While;}
};
struct ForStmt : Stmt {
    StmtPtr init; ExprPtr cond, step; StmtPtr body;
    ForStmt(StmtPtr i, ExprPtr c, ExprPtr s, StmtPtr b)
        :init(std::move(i)),cond(std::move(c)),step(std::move(s)),body(std::move(b)){kind=Kind::For;}
};
struct ReturnStmt : Stmt { ExprPtr value; explicit ReturnStmt(ExprPtr v):value(std::move(v)){kind=Kind::Return;} };
struct PrintStmt : Stmt { ExprPtr value; explicit PrintStmt(ExprPtr v):value(std::move(v)){kind=Kind::Print;} };
struct BreakStmt : Stmt { BreakStmt(){kind=Kind::Break;} };
struct ContinueStmt : Stmt { ContinueStmt(){kind=Kind::Continue;} };

struct LoopStmt : Stmt {
    StmtPtr body;
    explicit LoopStmt(StmtPtr b):body(std::move(b)){kind=Kind::Loop;}
};
struct DoWhileStmt : Stmt {
    StmtPtr body; ExprPtr cond;
    DoWhileStmt(StmtPtr b, ExprPtr c):body(std::move(b)),cond(std::move(c)){kind=Kind::DoWhile;}
};
struct AssertStmt : Stmt {
    ExprPtr cond;
    explicit AssertStmt(ExprPtr c):cond(std::move(c)){kind=Kind::Assert;}
};

struct Param { std::string type, name; bool isArray = false; };
struct FunctionDeclStmt : Stmt {
    std::string returnType, name;
    std::vector<Param> params;
    BlockPtr body;
    FunctionDeclStmt(std::string rt, std::string n)
        :returnType(std::move(rt)),name(std::move(n)){kind=Kind::FuncDecl;}
};

struct Program { std::vector<StmtPtr> decls; };

class Parser {
public:
    struct ParseError : std::runtime_error {
        int line, col;
        ParseError(const std::string& m, int l, int c)
            :std::runtime_error(m),line(l),col(c){}
    };

    explicit Parser(std::vector<Token> tokens);
    Program parseProgram();
    std::string dump(const Program& p);

private:
    std::vector<Token> tokens;
    size_t position = 0;

    const Token& peek() const;
    bool isAtEnd() const;
    Token advance();
    bool checkType(TokenType t) const;
    bool checkKeyword(const std::string& k) const;
    bool checkPunct(const std::string& p) const;
    bool checkOp(const std::string& o) const;
    bool matchType(TokenType t);
    bool matchKeyword(const std::string& k);
    bool matchPunct(const std::string& p);
    bool matchOp(const std::string& o);
    [[noreturn]] void error(const std::string& m) const;

    StmtPtr declaration();
    StmtPtr statement();
    StmtPtr varDecl();
    StmtPtr funcDecl();
    StmtPtr block();
    StmtPtr ifStatement();
    StmtPtr whileStatement();
    StmtPtr forStatement();
    StmtPtr loopStatement();
    StmtPtr doWhileStatement();
    StmtPtr assertStatement();
    StmtPtr returnStatement();
    StmtPtr printStatement();
    StmtPtr breakStatement();
    StmtPtr continueStatement();
    StmtPtr exprStatement();

    ExprPtr expression();
    ExprPtr logicalOr();
    ExprPtr logicalAnd();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr additive();
    ExprPtr multiplicative();
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary();
};
#endif
