#ifndef INTERPRETER_HPP
#define INTERPRETER_HPP
#include "semantic.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct Value {
    Type type = Type::Int;
    bool isArray = false;
    long i = 0;
    double f = 0.0;
    std::string s;
    std::vector<Value> arr;

    static Value makeInt(long v)     { Value r; r.type=Type::Int; r.i=v; return r; }
    static Value makeFloat(double v) { Value r; r.type=Type::Float; r.f=v; return r; }
    static Value makeString(std::string v){ Value r; r.type=Type::String; r.s=std::move(v); return r; }

    double asDouble() const { return type==Type::Float ? f : (double)i; }
    long   asInt()    const { return type==Type::Float ? (long)f : i; }
    bool   isTruthy() const {
        if (type==Type::Int)    return i != 0;
        if (type==Type::Float)  return f != 0.0;
        if (type==Type::String) return !s.empty();
        return false;
    }
};

class Environment {
public:
    explicit Environment(Environment* p = nullptr) : parent_(p) {}
    bool declare(const std::string& n, Value v) {
        if (vars_.count(n)) return false;
        vars_[n] = std::move(v); return true;
    }
    Value* lookup(const std::string& n) {
        if (auto it = vars_.find(n); it != vars_.end()) return &it->second;
        return parent_ ? parent_->lookup(n) : nullptr;
    }
    Environment* parent() const { return parent_; }
private:
    Environment* parent_;
    std::unordered_map<std::string, Value> vars_;
};

class Interpreter {
public:
    void run(Program& p);
    int exitCode() const { return exitCode_; }

private:
    struct ReturnSignal   { Value value; };
    struct BreakSignal    {};
    struct ContinueSignal {};

    std::vector<std::unique_ptr<Environment>> envs_;
    Environment* current_ = nullptr;
    std::unordered_map<std::string, FunctionDeclStmt*> functions_;
    int exitCode_ = 0;

    void enterScope();
    void exitScope();
    Value evalExpr(Expr* e);
    void  execStmt(Stmt* s);
    Value applyBinary(const std::string& op, const Value& l, const Value& r);
    void  printValue(const Value& v);
};
#endif
