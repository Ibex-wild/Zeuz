#include "interpreter.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

void Interpreter::enterScope() {
    auto e = std::make_unique<Environment>(current_);
    current_ = e.get();
    envs_.push_back(std::move(e));
}
void Interpreter::exitScope() {
    if (envs_.size() <= 1) return;
    current_ = current_->parent();
    envs_.pop_back();
}

static void printValNoNL(const Value& v) {
    if (v.type == Type::Int) std::cout << v.i;
    else if (v.type == Type::Float) {
        std::cout << v.f;
        if (std::isfinite(v.f) && v.f == (long)v.f) std::cout << ".0";
    }
    else if (v.type == Type::String) std::cout << v.s;
}

#define printValueNoNewline printValNoNL

void Interpreter::printValue(const Value& v) {
    if (v.isArray) {
        std::cout << "[";
        for (size_t i = 0; i < v.arr.size(); i++) {
            if (i) std::cout << ", ";
            printValueNoNewline(v.arr[i]);
        }
        std::cout << "]\n";
        return;
    }
    printValueNoNewline(v);
    std::cout << "\n";
}

void Interpreter::run(Program& p) {
    for (auto& s : p.decls) {
        if (s->kind == Stmt::Kind::FuncDecl) {
            auto* f = static_cast<FunctionDeclStmt*>(s.get());
            functions_[f->name] = f;
        }
    }
    enterScope();
    try {
        auto it = functions_.find("entryf");
        if (it != functions_.end()) {
            execStmt(it->second->body.get());
        } else {
            throw std::runtime_error("runtime error: entryf() function not found!");
        }
    } catch (ReturnSignal& sig) {
        if (sig.value.type != Type::Void) exitCode_ = (int)sig.value.asInt();
    } catch (BreakSignal&) {
        std::cerr << "runtime: break outside loop\n";
        exitCode_ = 1;
    } catch (ContinueSignal&) {
        std::cerr << "runtime: continue outside loop\n";
        exitCode_ = 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        exitCode_ = 1;
    }
    exitScope();
}

Value Interpreter::applyBinary(const std::string& op, const Value& l, const Value& r) {
    if (op == "+" && (l.type == Type::String || r.type == Type::String)) {
        auto toStr = [](const Value& v) {
            if (v.type == Type::String) return v.s;
            if (v.type == Type::Int) return std::to_string(v.i);
            std::ostringstream os; os << v.f; return os.str();
        };
        return Value::makeString(toStr(l) + toStr(r));
    }
    if (op == "&&") return Value::makeInt(l.isTruthy() && r.isTruthy());
    if (op == "||") return Value::makeInt(l.isTruthy() || r.isTruthy());

    if (op == "==") return Value::makeInt(l.asDouble() == r.asDouble());
    if (op == "!=") return Value::makeInt(l.asDouble() != r.asDouble());
    if (op == "<")  return Value::makeInt(l.asDouble() <  r.asDouble());
    if (op == ">")  return Value::makeInt(l.asDouble() >  r.asDouble());
    if (op == "<=") return Value::makeInt(l.asDouble() <= r.asDouble());
    if (op == ">=") return Value::makeInt(l.asDouble() >= r.asDouble());

    bool usef = (l.type == Type::Float || r.type == Type::Float);
    if (op == "+") return usef ? Value::makeFloat(l.asDouble()+r.asDouble()) : Value::makeInt(l.i+r.i);
    if (op == "-") return usef ? Value::makeFloat(l.asDouble()-r.asDouble()) : Value::makeInt(l.i-r.i);
    if (op == "*") return usef ? Value::makeFloat(l.asDouble()*r.asDouble()) : Value::makeInt(l.i*r.i);
    if (op == "/") {
        if (r.asDouble() == 0.0) throw std::runtime_error("division by zero");
        return usef ? Value::makeFloat(l.asDouble()/r.asDouble()) : Value::makeInt(l.i/r.i);
    }
    throw std::runtime_error("unknown operator: " + op);
}

Value Interpreter::evalExpr(Expr* e) {
    switch (e->kind) {
        case Expr::Kind::IntLit:    return Value::makeInt(static_cast<IntLit*>(e)->value);
        case Expr::Kind::FloatLit:  return Value::makeFloat(static_cast<FloatLit*>(e)->value);
        case Expr::Kind::StringLit: return Value::makeString(static_cast<StringLit*>(e)->value);
        case Expr::Kind::Var: {
            auto* v = static_cast<VarExpr*>(e);
            auto* slot = current_->lookup(v->name);
            if (!slot) throw std::runtime_error("undefined variable: " + v->name);
            return *slot;
        }
        case Expr::Kind::Binary: {
            auto* b = static_cast<BinaryExpr*>(e);
            if (b->op == "&&") {
                Value l = evalExpr(b->left.get());
                if (!l.isTruthy()) return Value::makeInt(0);
                return Value::makeInt(evalExpr(b->right.get()).isTruthy());
            }
            if (b->op == "||") {
                Value l = evalExpr(b->left.get());
                if (l.isTruthy()) return Value::makeInt(1);
                return Value::makeInt(evalExpr(b->right.get()).isTruthy());
            }
            return applyBinary(b->op, evalExpr(b->left.get()), evalExpr(b->right.get()));
        }
        case Expr::Kind::Unary: {
            auto* u = static_cast<UnaryExpr*>(e);
            Value v = evalExpr(u->operand.get());
            if (u->op == "-") return v.type == Type::Float ? Value::makeFloat(-v.f) : Value::makeInt(-v.i);
            if (u->op == "!") return Value::makeInt(!v.isTruthy());
            throw std::runtime_error("unknown unary: " + u->op);
        }
        case Expr::Kind::Assign: {
            auto* a = static_cast<AssignExpr*>(e);
            Value v = evalExpr(a->value.get());
            auto* slot = current_->lookup(a->name);
            if (!slot) throw std::runtime_error("undefined variable: " + a->name);
            if (slot->type == Type::Float && v.type == Type::Int) v = Value::makeFloat((double)v.i);
            *slot = v;
            return v;
        }
        case Expr::Kind::AssignIndex: {
            auto* a = static_cast<AssignIndexExpr*>(e);
            auto* slot = current_->lookup(a->name);
            if (!slot) throw std::runtime_error("undefined array: " + a->name);
            long idx = evalExpr(a->index.get()).asInt();
            if (idx < 0 || idx >= (long)slot->arr.size())
                throw std::runtime_error("array index out of bounds: " + a->name + "[" + std::to_string(idx) + "]");
            Value v = evalExpr(a->value.get());
            if (slot->type == Type::Float && v.type == Type::Int) v = Value::makeFloat((double)v.i);
            slot->arr[idx] = v;
            return v;
        }
        case Expr::Kind::Call: {
            auto* c = static_cast<CallExpr*>(e);

            if (c->name == "len") {
                if (c->args.size() != 1) 
                    throw std::runtime_error("len() expects 1 argument");
                Value argVal = evalExpr(c->args[0].get());
                if (argVal.isArray) {
                    return Value::makeInt((long)argVal.arr.size());
                } else if (argVal.type == Type::String) {
                    return Value::makeInt((long)argVal.s.size());
                }
                throw std::runtime_error("len() argument must be an array or string");
            }

            if (c->name == "input") {
              if (!c->args.empty()) {
                Value prompt = evalExpr(c->args[0].get());
                std::cout << prompt.s;
              }
              std::string line;
              std::getline(std::cin, line);
              return Value::makeString(line);
            }
            auto it = functions_.find(c->name);
            if (it == functions_.end()) throw std::runtime_error("undefined function: " + c->name);
            auto* fn = it->second;
            if (c->args.size() != fn->params.size())
                throw std::runtime_error("wrong number of arguments to " + c->name);

            enterScope();
            for (size_t i = 0; i < fn->params.size(); i++) {
                Value arg = evalExpr(c->args[i].get());
                if (fn->params[i].type == "float" && arg.type == Type::Int)
                    arg = Value::makeFloat((double)arg.i);
                current_->declare(fn->params[i].name, arg);
            }
            Value result = Value::makeInt(0);
            try {
                execStmt(fn->body.get());
            } catch (ReturnSignal& sig) {
                result = sig.value;
            } catch (...) { exitScope(); throw; }
            exitScope();
            return result;
        }
        case Expr::Kind::Index: {
            auto* ix = static_cast<IndexExpr*>(e);
            auto* slot = current_->lookup(ix->name);
            if (!slot) throw std::runtime_error("undefined array: " + ix->name);
            long idx = evalExpr(ix->index.get()).asInt();
            if (idx < 0 || idx >= (long)slot->arr.size())
                throw std::runtime_error("array index out of bounds: " + ix->name + "[" + std::to_string(idx) + "]");
            return slot->arr[idx];
        }
    }
    throw std::runtime_error("unknown expr");
}

void Interpreter::execStmt(Stmt* s) {
    switch (s->kind) {
        case Stmt::Kind::VarDecl: {
            auto* v = static_cast<VarDeclStmt*>(s);
            bool isFloat = (v->type == "float");
            bool isString = (v->type == "string" || v->type == "string");
            if (v->arraySize) {
                long n = evalExpr(v->arraySize.get()).asInt();
                if (n < 0) throw std::runtime_error("negative array size");
                Value arr;
                arr.type = isFloat ? Type::Float : Type::Int;
                arr.isArray = true;
                Type elemT = arr.type;
                for (auto& e : v->arrayInit) {
                    Value ev = evalExpr(e.get());
                    if (elemT == Type::Float && ev.type == Type::Int) ev = Value::makeFloat((double)ev.i);
                    arr.arr.push_back(ev);
                }
                while ((long)arr.arr.size() < n)
                    arr.arr.push_back(elemT == Type::Float ? Value::makeFloat(0.0) : Value::makeInt(0));
                current_->declare(v->name, arr);
            } else {
                Value val;
                if (v->init) val = evalExpr(v->init.get());
                else if (isFloat) val = Value::makeFloat(0.0);
                else if (isString) val = Value::makeString("");
                else val = Value::makeInt(0);
                if (isFloat && val.type == Type::Int) val = Value::makeFloat((double)val.i);
                current_->declare(v->name, val);
            }
            break;
        }
        case Stmt::Kind::ExprStmt: evalExpr(static_cast<ExprStmt*>(s)->expr.get()); break;
        case Stmt::Kind::Block:
            enterScope();
            for (auto& st : static_cast<BlockStmt*>(s)->stmts) execStmt(st.get());
            exitScope();
            break;
        case Stmt::Kind::If: {
            auto* f = static_cast<IfStmt*>(s);
            if (evalExpr(f->cond.get()).isTruthy()) execStmt(f->thenBranch.get());
            else if (f->elseBranch) execStmt(f->elseBranch.get());
            break;
        }
        case Stmt::Kind::While: {
            auto* w = static_cast<WhileStmt*>(s);
            while (evalExpr(w->cond.get()).isTruthy()) {
                try {
                    execStmt(w->body.get());
                } catch (ContinueSignal&) { continue; }
                catch (BreakSignal&)    { break; }
            }
            break;
        }
        case Stmt::Kind::For: {
            auto* f = static_cast<ForStmt*>(s);
            enterScope();
            if (f->init) execStmt(f->init.get());
            while (true) {
                if (f->cond && !evalExpr(f->cond.get()).isTruthy()) break;
                bool shouldBreak = false;
                try {
                    execStmt(f->body.get());
                } catch (ContinueSignal&) {}
                catch (BreakSignal&)    { shouldBreak = true; }
                if (shouldBreak) break;
                if (f->step) evalExpr(f->step.get());
            }
            exitScope();
            break;
        }
        case Stmt::Kind::Return: {
            auto* r = static_cast<ReturnStmt*>(s);
            ReturnSignal sig;
            if (r->value) sig.value = evalExpr(r->value.get());
            throw sig;
        }
        case Stmt::Kind::Print: {
            auto* p = static_cast<PrintStmt*>(s);
            printValue(evalExpr(p->value.get()));
            break;
        }
        case Stmt::Kind::Break: throw BreakSignal{};
        case Stmt::Kind::Continue: throw ContinueSignal{};
        case Stmt::Kind::FuncDecl: break;
    }
}
