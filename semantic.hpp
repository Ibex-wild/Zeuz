#ifndef SEMANTIC_HPP
#define SEMANTIC_HPP
#include "parser.hpp"
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

enum class Type { Int, Float, String, Void, Error };
std::string typeToString(Type t);

struct SymbolInfo { Type type; bool initialized; bool isArray; };
struct FunctionInfo { Type returnType; std::vector<Type> paramTypes; };

class Scope {
public:
    explicit Scope(Scope* p = nullptr) : parent_(p) {}
    bool declare(const std::string& n, Type t, bool init, bool arr);
    SymbolInfo* lookup(const std::string& n);
    SymbolInfo* lookupLocal(const std::string& n);
    Scope* parent() const { return parent_; }
private:
    Scope* parent_;
    std::unordered_map<std::string, SymbolInfo> symbols_;
};

class SemanticAnalyzer {
public:
    bool analyze(Program& p);

    const std::vector<std::string>& errors()   const { return errors_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    void printAnnotated(Program& p, std::ostream& os) const;

    const std::unordered_map<Expr*, Type>& exprTypes() const { return exprTypes_; }

private:
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
    std::vector<std::unique_ptr<Scope>> scopes_;
    Scope* current_ = nullptr;
    std::unordered_map<Expr*, Type> exprTypes_;
    std::unordered_map<std::string, FunctionInfo> functions_;
    int loopDepth_ = 0;
    int funcDepth_ = 0;
    Type currentReturnType_ = Type::Void;

    void error(int l, int c, const std::string& m);
    void warning(int l, int c, const std::string& m);

    void enterScope();
    void exitScope();

    Type typeOfExpr(Expr* e);
    Type parseType(const std::string& t) const;
    Type promote(Type a, Type b) const;

    void analyzeStmt(Stmt* s);
    void analyzeVarDecl(VarDeclStmt* v);
    void analyzeBlock(BlockStmt* b);
    void analyzeIf(IfStmt* s);
    void analyzeWhile(WhileStmt* s);
    void analyzeFor(ForStmt* s);
    void analyzeReturn(ReturnStmt* s);
    void analyzeFuncDecl(FunctionDeclStmt* f);

    void dumpExpr(Expr* e, int d, std::ostream& os) const;
    void dumpStmt(Stmt* s, int d, std::ostream& os) const;
};
#endif
