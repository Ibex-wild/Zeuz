#include "semantic.hpp"
#include <sstream>

std::string typeToString(Type t) {
    switch (t) {
        case Type::Int:    return "int";
        case Type::Float:  return "float";
        case Type::String: return "string";
        case Type::Void:   return "void";
        case Type::Error:  return "<error>";
    }
    return "?";
}

bool Scope::declare(const std::string& n, Type t, bool init, bool arr) {
    if (symbols_.count(n)) return false;
    symbols_[n] = { t, init, arr };
    return true;
}
SymbolInfo* Scope::lookup(const std::string& n) {
    if (auto it = symbols_.find(n); it != symbols_.end()) return &it->second;
    return parent_ ? parent_->lookup(n) : nullptr;
}
SymbolInfo* Scope::lookupLocal(const std::string& n) {
    if (auto it = symbols_.find(n); it != symbols_.end()) return &it->second;
    return nullptr;
}

void SemanticAnalyzer::error(int l, int c, const std::string& m) {
    errors_.push_back(std::to_string(l) + ":" + std::to_string(c) + ": " + m);
}
void SemanticAnalyzer::warning(int l, int c, const std::string& m) {
    warnings_.push_back(std::to_string(l) + ":" + std::to_string(c) + ": " + m);
}

void SemanticAnalyzer::enterScope() {
    auto s = std::make_unique<Scope>(current_);
    current_ = s.get();
    scopes_.push_back(std::move(s));
}
void SemanticAnalyzer::exitScope() {
    if (scopes_.size() <= 1) return;
    current_ = current_->parent();
    scopes_.pop_back();
}

Type SemanticAnalyzer::parseType(const std::string& t) const {
    if (t == "int" || t == "bool" || t == "char" || t == "byte"
        || t == "i8" || t == "i16" || t == "i32" || t == "i64" || t == "i128"
        || t == "u8" || t == "u16" || t == "u32" || t == "u64" || t == "u128"
        || t == "usize" || t == "isize")
        return Type::Int;
    if (t == "float" || t == "f16" || t == "f32" || t == "f64")
        return Type::Float;
    if (t == "string") return Type::String;
    if (t == "void")   return Type::Void;
    return Type::Error;
}

Type SemanticAnalyzer::promote(Type a, Type b) const {
    if (a == Type::Error || b == Type::Error) return Type::Error;
    if (a == Type::String || b == Type::String) return Type::String;
    if (a == Type::Float || b == Type::Float) return Type::Float;
    return Type::Int;
}

bool SemanticAnalyzer::analyze(Program& program) {
    errors_.clear(); warnings_.clear();
    exprTypes_.clear(); scopes_.clear(); functions_.clear();
    current_ = nullptr;
    loopDepth_ = funcDepth_ = 0;
    currentReturnType_ = Type::Void;

    enterScope();

    functions_["len"]   = { Type::Int,    { Type::Error } };
    functions_["input"] = { Type::String, { Type::String } };

    for (auto& s : program.decls) {
        if (s->kind == Stmt::Kind::FuncDecl) {
            auto* f = static_cast<FunctionDeclStmt*>(s.get());
            FunctionInfo info;
            info.returnType = parseType(f->returnType);
            for (auto& p : f->params) info.paramTypes.push_back(parseType(p.type));
            if (functions_.count(f->name))
                error(f->line, f->col, "redeclaration of function '" + f->name + "'");
            else
                functions_[f->name] = info;
        }
    }

    for (auto& s : program.decls) analyzeStmt(s.get());

    exitScope();
    return errors_.empty();
}

Type SemanticAnalyzer::typeOfExpr(Expr* e) {
    if (!e) return Type::Error;
    Type t = Type::Error;

    switch (e->kind) {
        case Expr::Kind::IntLit:    t = Type::Int;    break;
        case Expr::Kind::FloatLit:  t = Type::Float;  break;
        case Expr::Kind::StringLit: t = Type::String; break;

        case Expr::Kind::Var: {
            auto* v = static_cast<VarExpr*>(e);
            if (auto* sym = current_->lookup(v->name)) {
                if (!sym->initialized)
                    warning(e->line, e->col,
                            "use of uninitialized variable '" + v->name + "'");
                t = sym->type;
            } else {
                error(e->line, e->col, "undeclared variable '" + v->name + "'");
            }
            break;
        }

        case Expr::Kind::Binary: {
            auto* b = static_cast<BinaryExpr*>(e);
            Type l = typeOfExpr(b->left.get());
            Type r = typeOfExpr(b->right.get());
            if (l == Type::Error || r == Type::Error) { t = Type::Error; break; }
            if (b->op == "==" || b->op == "!=" || b->op == "<" || b->op == ">" ||
                b->op == "<=" || b->op == ">=")             { t = Type::Int; break; }
            if (b->op == "&&" || b->op == "||")             { t = Type::Int; break; }
            if (b->op == "+" && (l == Type::String || r == Type::String))
                                                            { t = Type::String; break; }
            t = promote(l, r);
            break;
        }

        case Expr::Kind::Unary: {
            auto* u = static_cast<UnaryExpr*>(e);
            if (u->op.rfind("cast_", 0) == 0) {
                t = parseType(u->op.substr(5));
                typeOfExpr(u->operand.get());
            } else {
                t = typeOfExpr(u->operand.get());
            }
            break;
        }

        case Expr::Kind::Assign: {
            auto* a = static_cast<AssignExpr*>(e);
            auto* sym = current_->lookup(a->name);
            if (!sym) {
                error(e->line, e->col,
                      "assignment to undeclared variable '" + a->name + "'");
                typeOfExpr(a->value.get());
                break;
            }
            if (sym->isArray) {
                error(e->line, e->col,
                      "cannot assign to array '" + a->name + "' as a whole");
                typeOfExpr(a->value.get());
                break;
            }
            Type vt = typeOfExpr(a->value.get());
            if (vt == Type::Error) break;
            if (sym->type == Type::Int && vt == Type::Float)
                error(e->line, e->col,
                      "cannot assign float to int variable '" + a->name + "'");
            else {
                sym->initialized = true;
                t = sym->type;
            }
            break;
        }

        case Expr::Kind::AssignIndex: {
            auto* a = static_cast<AssignIndexExpr*>(e);
            auto* sym = current_->lookup(a->name);
            if (!sym) {
                error(e->line, e->col, "undeclared array '" + a->name + "'");
                break;
            }
            if (!sym->isArray) {
                error(e->line, e->col, "'" + a->name + "' is not an array");
                break;
            }
            typeOfExpr(a->index.get());
            Type vt = typeOfExpr(a->value.get());
            if (sym->type == Type::Int && vt == Type::Float)
                error(e->line, e->col, "cannot assign float to int array element");
            else t = sym->type;
            break;
        }

        case Expr::Kind::Call: {
            auto* c = static_cast<CallExpr*>(e);
            auto it = functions_.find(c->name);
            if (it == functions_.end()) {
                error(e->line, e->col, "undeclared function '" + c->name + "'");
                break;
            }
            auto& info = it->second;
            if (c->args.size() != info.paramTypes.size()) {
                error(e->line, e->col,
                      "function '" + c->name + "' expects " +
                      std::to_string(info.paramTypes.size()) + " args, got " +
                      std::to_string(c->args.size()));
                break;
            }
            for (size_t i = 0; i < c->args.size(); i++) {
                Type at = typeOfExpr(c->args[i].get());
                Type pt = info.paramTypes[i];
                if (at != Type::Error && pt == Type::Int && at == Type::Float)
                    error(e->line, e->col,
                          "arg " + std::to_string(i+1) + " of '" + c->name +
                          "': cannot pass float as int");
                if (at != Type::Error && pt == Type::String && at != Type::String)
                    error(e->line, e->col,
                          "arg " + std::to_string(i+1) + " of '" + c->name +
                          "': expects string");
            }
            t = info.returnType;
            break;
        }

        case Expr::Kind::Index: {
            auto* ix = static_cast<IndexExpr*>(e);
            auto* sym = current_->lookup(ix->name);
            if (!sym) {
                error(e->line, e->col, "undeclared array '" + ix->name + "'");
                break;
            }
            if (!sym->isArray) {
                error(e->line, e->col, "'" + ix->name + "' is not an array");
                break;
            }
            typeOfExpr(ix->index.get());
            t = sym->type;
            break;
        }
    }

    exprTypes_[e] = t;
    return t;
}

void SemanticAnalyzer::analyzeStmt(Stmt* s) {
    switch (s->kind) {
        case Stmt::Kind::VarDecl:  analyzeVarDecl(static_cast<VarDeclStmt*>(s)); break;
        case Stmt::Kind::ExprStmt: typeOfExpr(static_cast<ExprStmt*>(s)->expr.get()); break;
        case Stmt::Kind::Block:    analyzeBlock(static_cast<BlockStmt*>(s)); break;
        case Stmt::Kind::If:       analyzeIf(static_cast<IfStmt*>(s)); break;
        case Stmt::Kind::While:    analyzeWhile(static_cast<WhileStmt*>(s)); break;
        case Stmt::Kind::For:      analyzeFor(static_cast<ForStmt*>(s)); break;
        case Stmt::Kind::Return:   analyzeReturn(static_cast<ReturnStmt*>(s)); break;
        case Stmt::Kind::Print:    typeOfExpr(static_cast<PrintStmt*>(s)->value.get()); break;

        case Stmt::Kind::Loop:
            loopDepth_++;
            analyzeStmt(static_cast<LoopStmt*>(s)->body.get());
            loopDepth_--;
            break;

        case Stmt::Kind::DoWhile: {
            auto* dw = static_cast<DoWhileStmt*>(s);
            loopDepth_++;
            analyzeStmt(dw->body.get());
            loopDepth_--;
            typeOfExpr(dw->cond.get());
            break;
        }
        case Stmt::Kind::Assert:
            typeOfExpr(static_cast<AssertStmt*>(s)->cond.get());
            break;
        case Stmt::Kind::Break:
            if (loopDepth_ == 0) error(s->line, s->col, "'break' outside of loop");
            break;
        case Stmt::Kind::Continue:
            if (loopDepth_ == 0) error(s->line, s->col, "'continue' outside of loop");
            break;
        case Stmt::Kind::FuncDecl:
            analyzeFuncDecl(static_cast<FunctionDeclStmt*>(s));
            break;
    }
}

void SemanticAnalyzer::analyzeVarDecl(VarDeclStmt* v) {
    Type t = parseType(v->type);
    bool isArray = (v->arraySize != nullptr);
    if (!current_->declare(v->name, t, false, isArray)) {
        error(v->line, v->col, "redeclaration of variable '" + v->name + "'");
        return;
    }
    if (isArray) {
        Type st = typeOfExpr(v->arraySize.get());
        if (st != Type::Int && st != Type::Error)
            error(v->line, v->col, "array size must be int");
        for (auto& e : v->arrayInit) {
            Type et = typeOfExpr(e.get());
            if (t == Type::Int && et == Type::Float)
                error(v->line, v->col, "float in int array initializer");
        }
        if (auto* sym = current_->lookupLocal(v->name)) sym->initialized = true;
    } else if (v->init) {
        Type it = typeOfExpr(v->init.get());
        if (it != Type::Error) {
            if (t == Type::Int && it == Type::Float)
                error(v->line, v->col, "cannot initialize int '" + v->name + "' with float");
            else if (t == Type::String && it != Type::String)
                error(v->line, v->col, "cannot initialize string '" + v->name + "'");
            else if (auto* sym = current_->lookupLocal(v->name)) sym->initialized = true;
        }
    } else {
        if (auto* sym = current_->lookupLocal(v->name)) sym->initialized = true;
    }
}

void SemanticAnalyzer::analyzeBlock(BlockStmt* b) {
    enterScope();
    for (auto& s : b->stmts) analyzeStmt(s.get());
    exitScope();
}

void SemanticAnalyzer::analyzeIf(IfStmt* s) {
    typeOfExpr(s->cond.get());
    analyzeStmt(s->thenBranch.get());
    if (s->elseBranch) analyzeStmt(s->elseBranch.get());
}

void SemanticAnalyzer::analyzeWhile(WhileStmt* s) {
    typeOfExpr(s->cond.get());
    loopDepth_++;
    analyzeStmt(s->body.get());
    loopDepth_--;
}

void SemanticAnalyzer::analyzeFor(ForStmt* s) {
    enterScope();
    if (s->init) analyzeStmt(s->init.get());
    if (s->cond) typeOfExpr(s->cond.get());
    if (s->step) typeOfExpr(s->step.get());
    loopDepth_++;
    analyzeStmt(s->body.get());
    loopDepth_--;
    exitScope();
}

void SemanticAnalyzer::analyzeReturn(ReturnStmt* s) {
    Type t = s->value ? typeOfExpr(s->value.get()) : Type::Void;
    if (funcDepth_ == 0) {
        warning(s->line, s->col, "'return' outside function");
        return;
    }
    if (currentReturnType_ == Type::Void) {
        if (s->value)
            error(s->line, s->col, "void function cannot return a value");
        return;
    }
    if (!s->value) {
        error(s->line, s->col, "missing return value");
        return;
    }
    if (t == Type::Error) return;
    if (currentReturnType_ == Type::Int && t == Type::Float)
        error(s->line, s->col, "cannot return float from int function");
    if (currentReturnType_ == Type::String && t != Type::String)
        error(s->line, s->col, "cannot return non-string from string function");
}

void SemanticAnalyzer::analyzeFuncDecl(FunctionDeclStmt* f) {
    Type retT = parseType(f->returnType);
    Type prevRet = currentReturnType_;
    currentReturnType_ = retT;
    funcDepth_++;

    enterScope();
    for (auto& p : f->params) {
        Type pt = parseType(p.type);
        if (!current_->declare(p.name, pt, true, p.isArray))
            error(f->line, f->col, "duplicate parameter '" + p.name + "'");
    }
    analyzeBlock(f->body.get());
    exitScope();

    funcDepth_--;
    currentReturnType_ = prevRet;
}

void SemanticAnalyzer::dumpExpr(Expr* e, int d, std::ostream& os) const {
    std::string ind(d*2, ' ');
    Type t = Type::Error;
    if (auto it = exprTypes_.find(e); it != exprTypes_.end()) t = it->second;

    os << ind;
    switch (e->kind) {
        case Expr::Kind::IntLit:
            os << "IntLit(" << static_cast<IntLit*>(e)->value << ") : "
               << typeToString(t) << "\n"; break;
        case Expr::Kind::FloatLit:
            os << "FloatLit(" << static_cast<FloatLit*>(e)->value << ") : "
               << typeToString(t) << "\n"; break;
        case Expr::Kind::StringLit:
            os << "StringLit(\"" << static_cast<StringLit*>(e)->value
               << "\") : string\n"; break;
        case Expr::Kind::Var:
            os << "Var(" << static_cast<VarExpr*>(e)->name << ") : "
               << typeToString(t) << "\n"; break;
        case Expr::Kind::Binary: {
            auto* b = static_cast<BinaryExpr*>(e);
            os << "Binary(" << b->op << ") : " << typeToString(t) << "\n";
            dumpExpr(b->left.get(), d+1, os);
            dumpExpr(b->right.get(), d+1, os);
            break;
        }
        case Expr::Kind::Unary: {
            auto* u = static_cast<UnaryExpr*>(e);
            os << "Unary(" << u->op << ") : " << typeToString(t) << "\n";
            dumpExpr(u->operand.get(), d+1, os);
            break;
        }
        case Expr::Kind::Assign: {
            auto* a = static_cast<AssignExpr*>(e);
            os << "Assign(" << a->name << ") : " << typeToString(t) << "\n";
            dumpExpr(a->value.get(), d+1, os);
            break;
        }
        case Expr::Kind::AssignIndex: {
            auto* a = static_cast<AssignIndexExpr*>(e);
            os << "AssignIndex(" << a->name << ") : " << typeToString(t) << "\n";
            dumpExpr(a->index.get(), d+1, os);
            dumpExpr(a->value.get(), d+1, os);
            break;
        }
        case Expr::Kind::Call: {
            auto* c = static_cast<CallExpr*>(e);
            os << "Call(" << c->name << ") : " << typeToString(t) << "\n";
            for (auto& a : c->args) dumpExpr(a.get(), d+1, os);
            break;
        }
        case Expr::Kind::Index: {
            auto* ix = static_cast<IndexExpr*>(e);
            os << "Index(" << ix->name << ") : " << typeToString(t) << "\n";
            dumpExpr(ix->index.get(), d+1, os);
            break;
        }
    }
}

void SemanticAnalyzer::dumpStmt(Stmt* s, int d, std::ostream& os) const {
    std::string ind(d*2, ' ');
    switch (s->kind) {
        case Stmt::Kind::VarDecl: {
            auto* v = static_cast<VarDeclStmt*>(s);
            os << ind << "VarDecl(" << v->type << " " << v->name;
            if (v->arraySize) os << "[]";
            os << ")\n";
            if (v->init) dumpExpr(v->init.get(), d+1, os);
            if (v->arraySize) {
                os << ind << "  Size:\n";
                dumpExpr(v->arraySize.get(), d+2, os);
            }
            for (auto& e : v->arrayInit) dumpExpr(e.get(), d+1, os);
            break;
        }
        case Stmt::Kind::ExprStmt:
            os << ind << "ExprStmt\n";
            dumpExpr(static_cast<ExprStmt*>(s)->expr.get(), d+1, os);
            break;
        case Stmt::Kind::Block:
            os << ind << "Block\n";
            for (auto& st : static_cast<BlockStmt*>(s)->stmts)
                dumpStmt(st.get(), d+1, os);
            break;
        case Stmt::Kind::If: {
            auto* f = static_cast<IfStmt*>(s);
            os << ind << "If\n";
            dumpExpr(f->cond.get(), d+1, os);
            dumpStmt(f->thenBranch.get(), d+1, os);
            if (f->elseBranch) {
                os << ind << "  Else\n";
                dumpStmt(f->elseBranch.get(), d+2, os);
            }
            break;
        }
        case Stmt::Kind::While: {
            auto* w = static_cast<WhileStmt*>(s);
            os << ind << "While\n";
            dumpExpr(w->cond.get(), d+1, os);
            dumpStmt(w->body.get(), d+1, os);
            break;
        }
        case Stmt::Kind::For: {
            auto* f = static_cast<ForStmt*>(s);
            os << ind << "For\n";
            if (f->init) { os << ind << "  Init:\n"; dumpStmt(f->init.get(), d+2, os); }
            if (f->cond) { os << ind << "  Cond:\n"; dumpExpr(f->cond.get(), d+2, os); }
            if (f->step) { os << ind << "  Step:\n"; dumpExpr(f->step.get(), d+2, os); }
            os << ind << "  Body:\n";
            dumpStmt(f->body.get(), d+2, os);
            break;
        }
        case Stmt::Kind::Loop:
            os << ind << "Loop\n";
            dumpStmt(static_cast<LoopStmt*>(s)->body.get(), d+1, os);
            break;
        case Stmt::Kind::DoWhile: {
            auto* dw = static_cast<DoWhileStmt*>(s);
            os << ind << "DoWhile\n";
            os << ind << "  Body:\n";
            dumpStmt(dw->body.get(), d+2, os);
            os << ind << "  Cond:\n";
            dumpExpr(dw->cond.get(), d+2, os);
            break;
        }
        case Stmt::Kind::Assert:
            os << ind << "Assert\n";
            dumpExpr(static_cast<AssertStmt*>(s)->cond.get(), d+1, os);
            break;
        case Stmt::Kind::Return:
            os << ind << "Return\n";
            if (auto* r = static_cast<ReturnStmt*>(s); r->value)
                dumpExpr(r->value.get(), d+1, os);
            break;
        case Stmt::Kind::Print:
            os << ind << "Print\n";
            dumpExpr(static_cast<PrintStmt*>(s)->value.get(), d+1, os);
            break;
        case Stmt::Kind::Break:    os << ind << "Break\n";    break;
        case Stmt::Kind::Continue: os << ind << "Continue\n"; break;
        case Stmt::Kind::FuncDecl: {
            auto* f = static_cast<FunctionDeclStmt*>(s);
            os << ind << "Func(" << f->returnType << " " << f->name << ")\n";
            for (auto& p : f->params)
                os << ind << "  Param(" << p.type << " " << p.name
                   << (p.isArray ? "[]" : "") << ")\n";
            dumpStmt(f->body.get(), d+1, os);
            break;
        }
    }
}

void SemanticAnalyzer::printAnnotated(Program& p, std::ostream& os) const {
    os << "Program\n";
    for (auto& s : p.decls) dumpStmt(s.get(), 1, os);
}
