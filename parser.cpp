#include "parser.hpp"
#include <cmath>
#include <limits>
#include <set>
#include <sstream>

Parser::Parser(std::vector<Token> t) : tokens(std::move(t)) {}

const Token& Parser::peek() const {
    static const Token eof(TokenType::UNKNOWN, "<eof>");
    return isAtEnd() ? eof : tokens[position];
}
bool Parser::isAtEnd() const { return position >= tokens.size(); }
Token Parser::advance() { if (!isAtEnd()) position++; return tokens[position-1]; }

bool Parser::checkType(TokenType t) const { return !isAtEnd() && tokens[position].type == t; }
bool Parser::checkKeyword(const std::string& k) const { return checkType(TokenType::KEYWORD) && peek().value == k; }
bool Parser::checkPunct(const std::string& p) const { return checkType(TokenType::PUNCTUATOR) && peek().value == p; }
bool Parser::checkOp(const std::string& o) const { return checkType(TokenType::OPERATOR) && peek().value == o; }
bool Parser::matchType(TokenType t) { if (checkType(t)) { advance(); return true; } return false; }
bool Parser::matchKeyword(const std::string& k) { if (checkKeyword(k)) { advance(); return true; } return false; }
bool Parser::matchPunct(const std::string& p) { if (checkPunct(p)) { advance(); return true; } return false; }
bool Parser::matchOp(const std::string& o) { if (checkOp(o)) { advance(); return true; } return false; }

void Parser::error(const std::string& msg) const {
    std::string where = isAtEnd() ? "end of input" : "'" + peek().value + "'";
    int l = isAtEnd() ? (tokens.empty()?0:tokens.back().line) : peek().line;
    int c = isAtEnd() ? (tokens.empty()?0:tokens.back().col)  : peek().col;
    throw ParseError("at " + where + ": " + msg, l, c);
}

static const std::set<std::string>& typeKeywords() {
    static const std::set<std::string> s = {
        "int","float","string","void",
        "bool","char","byte",
        "i8","i16","i32","i64","i128",
        "u8","u16","u32","u64","u128",
        "usize","isize",
        "f16","f32","f64"
    };
    return s;
}

Program Parser::parseProgram() {
    Program p;
    while (!isAtEnd()) p.decls.push_back(declaration());
    return p;
}

StmtPtr Parser::declaration() {
    if (checkType(TokenType::KEYWORD) && typeKeywords().count(peek().value)) {
        if (position + 2 < tokens.size() &&
            tokens[position+1].type == TokenType::IDENTIFIER &&
            tokens[position+2].type == TokenType::PUNCTUATOR &&
            tokens[position+2].value == "(")
            return funcDecl();
        return varDecl();
    }
    if (checkType(TokenType::KEYWORD)) {
        static const std::set<std::string> reserved = {
            "enum","struct","type","class","trait","impl",
            "import","namespace","export","extern","macro",
            "fn","let","var","const"
        };
        if (reserved.count(peek().value))
            error("'" + peek().value + "' declarations not yet implemented");
    }
    return statement();
}

StmtPtr Parser::varDecl() {
    std::string type = advance().value;
    int l = peek().line, c = peek().col;
    if (!checkType(TokenType::IDENTIFIER)) error("expected identifier");
    std::string name = advance().value;

    auto d = std::make_unique<VarDeclStmt>(type, name, nullptr);
    d->line = l; d->col = c;

    if (matchPunct("[")) {
        d->arraySize = expression();
        if (!matchPunct("]")) error("expected ']'");
        if (matchOp("=")) {
            if (!matchPunct("{")) error("expected '{'");
            if (!checkPunct("}")) {
                d->arrayInit.push_back(expression());
                while (matchPunct(",")) d->arrayInit.push_back(expression());
            }
            if (!matchPunct("}")) error("expected '}'");
        }
    } else if (matchOp("=")) {
        d->init = expression();
    }
    if (!matchPunct(";")) error("expected ';'");
    return d;
}

StmtPtr Parser::funcDecl() {
    std::string rt = advance().value;
    int l = peek().line, c = peek().col;
    if (!checkType(TokenType::IDENTIFIER)) error("expected function name");
    auto fn = std::make_unique<FunctionDeclStmt>(rt, advance().value);
    fn->line = l; fn->col = c;

    if (!matchPunct("(")) error("expected '('");
    if (!checkPunct(")")) {
        while (true) {
            if (!checkType(TokenType::KEYWORD) || !typeKeywords().count(peek().value))
                error("expected parameter type");
            Param p;
            p.type = advance().value;
            if (!checkType(TokenType::IDENTIFIER)) error("expected parameter name");
            p.name = advance().value;
            if (matchPunct("[")) {
              p.isArray = true;
              if (!matchPunct("]")) error("expected ']'");
            }
            fn->params.push_back(p);
            if (!matchPunct(",")) break;
        }
    }
    if (!matchPunct(")")) error("expected ')'");
    fn->body.reset(static_cast<BlockStmt*>(block().release()));
    return fn;
}

StmtPtr Parser::statement() {
    int l = peek().line, c = peek().col;
    StmtPtr s;
    if (checkPunct("{"))            s = block();
    else if (matchKeyword("if"))    s = ifStatement();
    else if (matchKeyword("while")) s = whileStatement();
    else if (matchKeyword("for"))   s = forStatement();
    else if (matchKeyword("do"))    s = doWhileStatement();
    else if (matchKeyword("loop"))  s = loopStatement();
    else if (matchKeyword("return"))    s = returnStatement();
    else if (matchKeyword("print"))     s = printStatement();
    else if (matchKeyword("assert"))    s = assertStatement();
    else if (matchKeyword("break"))     s = breakStatement();
    else if (matchKeyword("continue"))  s = continueStatement();
    else if (checkType(TokenType::KEYWORD)) {
        error("'" + peek().value + "' statement not yet implemented");
    } else s = exprStatement();
    if (s) { s->line = l; s->col = c; }
    return s;
}

StmtPtr Parser::block() {
    if (!matchPunct("{")) error("expected '{'");
    auto b = std::make_unique<BlockStmt>();
    while (!checkPunct("}") && !isAtEnd()) b->stmts.push_back(declaration());
    if (!matchPunct("}")) error("expected '}'");
    return b;
}

StmtPtr Parser::ifStatement() {
    if (!matchPunct("(")) error("expected '('");
    auto c = expression();
    if (!matchPunct(")")) error("expected ')'");
    auto t = statement();
    StmtPtr e;
    if (matchKeyword("else")) e = statement();
    return std::make_unique<IfStmt>(std::move(c), std::move(t), std::move(e));
}

StmtPtr Parser::whileStatement() {
    if (!matchPunct("(")) error("expected '('");
    auto c = expression();
    if (!matchPunct(")")) error("expected ')'");
    return std::make_unique<WhileStmt>(std::move(c), statement());
}

StmtPtr Parser::forStatement() {
    if (!matchPunct("(")) error("expected '('");
    StmtPtr init;
    if (checkType(TokenType::KEYWORD) && typeKeywords().count(peek().value))
        init = varDecl();
    else if (!matchPunct(";")) {
        init = std::make_unique<ExprStmt>(expression());
        if (!matchPunct(";")) error("expected ';'");
    }
    ExprPtr cond;
    if (!checkPunct(";")) cond = expression();
    if (!matchPunct(";")) error("expected ';'");
    ExprPtr step;
    if (!checkPunct(")")) step = expression();
    if (!matchPunct(")")) error("expected ')'");
    return std::make_unique<ForStmt>(std::move(init), std::move(cond),
                                     std::move(step), statement());
}

StmtPtr Parser::loopStatement() {
    return std::make_unique<LoopStmt>(statement());
}

StmtPtr Parser::doWhileStatement() {
    auto body = statement();
    if (!matchKeyword("while")) error("expected 'while' after do-block");
    if (!matchPunct("(")) error("expected '('");
    auto cond = expression();
    if (!matchPunct(")")) error("expected ')'");
    if (!matchPunct(";")) error("expected ';'");
    return std::make_unique<DoWhileStmt>(std::move(body), std::move(cond));
}

StmtPtr Parser::assertStatement() {
    if (!matchPunct("(")) error("expected '(' after 'assert'");
    auto cond = expression();
    if (!matchPunct(")")) error("expected ')'");
    if (!matchPunct(";")) error("expected ';'");
    return std::make_unique<AssertStmt>(std::move(cond));
}

StmtPtr Parser::returnStatement() {
    ExprPtr v;
    if (!checkPunct(";")) v = expression();
    if (!matchPunct(";")) error("expected ';'");
    return std::make_unique<ReturnStmt>(std::move(v));
}

StmtPtr Parser::printStatement() {
    auto v = expression();
    if (!matchPunct(";")) error("expected ';'");
    return std::make_unique<PrintStmt>(std::move(v));
}

StmtPtr Parser::breakStatement()    { if (!matchPunct(";")) error("expected ';'"); return std::make_unique<BreakStmt>(); }
StmtPtr Parser::continueStatement() { if (!matchPunct(";")) error("expected ';'"); return std::make_unique<ContinueStmt>(); }

StmtPtr Parser::exprStatement() {
    auto e = expression();
    if (!matchPunct(";")) error("expected ';'");
    return std::make_unique<ExprStmt>(std::move(e));
}

ExprPtr Parser::expression() { return logicalOr(); }

ExprPtr Parser::logicalOr() {
    auto l = logicalAnd();
    while (matchOp("||")) l = std::make_unique<BinaryExpr>("||", std::move(l), logicalAnd());
    return l;
}
ExprPtr Parser::logicalAnd() {
    auto l = equality();
    while (matchOp("&&")) l = std::make_unique<BinaryExpr>("&&", std::move(l), equality());
    return l;
}
ExprPtr Parser::equality() {
    auto l = comparison();
    while (checkOp("==")||checkOp("!=")) { std::string o = advance().value; l = std::make_unique<BinaryExpr>(o, std::move(l), comparison()); }
    return l;
}
ExprPtr Parser::comparison() {
    auto l = additive();
    while (checkOp("<")||checkOp(">")||checkOp("<=")||checkOp(">=")) { std::string o = advance().value; l = std::make_unique<BinaryExpr>(o, std::move(l), additive()); }
    return l;
}
ExprPtr Parser::additive() {
    auto l = multiplicative();
    while (checkOp("+")||checkOp("-")) { std::string o = advance().value; l = std::make_unique<BinaryExpr>(o, std::move(l), multiplicative()); }
    return l;
}
ExprPtr Parser::multiplicative() {
    auto l = unary();
    while (checkOp("*")||checkOp("/")) { std::string o = advance().value; l = std::make_unique<BinaryExpr>(o, std::move(l), unary()); }
    return l;
}
ExprPtr Parser::unary() {
    if (matchOp("-")) return std::make_unique<UnaryExpr>("-", unary());
    if (matchOp("!")) return std::make_unique<UnaryExpr>("!", unary());
    if (checkPunct("(") && position + 2 < tokens.size() &&
        tokens[position+1].type == TokenType::KEYWORD &&
        typeKeywords().count(tokens[position+1].value) &&
        tokens[position+2].type == TokenType::PUNCTUATOR &&
        tokens[position+2].value == ")") {
        advance();
        std::string targetType = advance().value;
        advance();
        return std::make_unique<UnaryExpr>("cast_" + targetType, unary());
    }
    return postfix();
}

ExprPtr Parser::postfix() {
    auto e = primary();
    while (true) {
        if (checkPunct("(")) {
            if (e->kind != Expr::Kind::Var) error("can only call identifiers");
            auto* v = static_cast<VarExpr*>(e.get());
            advance();
            auto call = std::make_unique<CallExpr>(v->name);
            call->line = v->line; call->col = v->col;
            if (!checkPunct(")")) {
                call->args.push_back(expression());
                while (matchPunct(",")) call->args.push_back(expression());
            }
            if (!matchPunct(")")) error("expected ')'");
            e = std::move(call);
        } else if (checkPunct("[")) {
            if (e->kind != Expr::Kind::Var) error("can only index identifiers");
            auto* v = static_cast<VarExpr*>(e.get());
            advance();
            auto idx = expression();
            if (!matchPunct("]")) error("expected ']'");
            auto node = std::make_unique<IndexExpr>(v->name, std::move(idx));
            node->line = v->line; node->col = v->col;
            e = std::move(node);
        } else if (checkOp("=")) {
            if (e->kind == Expr::Kind::Var) {
                auto* v = static_cast<VarExpr*>(e.get());
                advance();
                auto rhs = expression();
                auto node = std::make_unique<AssignExpr>(v->name, std::move(rhs));
                node->line = v->line; node->col = v->col;
                e = std::move(node);
            } else if (e->kind == Expr::Kind::Index) {
                auto* ix = static_cast<IndexExpr*>(e.get());
                advance();
                auto rhs = expression();
                auto node = std::make_unique<AssignIndexExpr>(ix->name, std::move(ix->index), std::move(rhs));
                node->line = ix->line; node->col = ix->col;
                e = std::move(node);
            } else error("invalid assignment target");
        } else break;
    }
    return e;
}

ExprPtr Parser::primary() {
    int l = peek().line, c = peek().col;
    ExprPtr e;
    if (checkType(TokenType::INTEGER_LITERAL))     e = std::make_unique<IntLit>(std::stol(advance().value));
    else if (checkType(TokenType::FLOAT_LITERAL))  e = std::make_unique<FloatLit>(std::stod(advance().value));
    else if (checkType(TokenType::STRING_LITERAL)) e = std::make_unique<StringLit>(advance().value);
    else if (checkType(TokenType::CHAR_LITERAL))   e = std::make_unique<IntLit>(std::stol(advance().value));
    else if (checkKeyword("true"))  { advance(); e = std::make_unique<IntLit>(1); }
    else if (checkKeyword("false")) { advance(); e = std::make_unique<IntLit>(0); }
    else if (checkKeyword("null") || checkKeyword("nullptr")) {
        advance(); e = std::make_unique<IntLit>(0);
    }
    else if (checkKeyword("inf")) {
        advance(); e = std::make_unique<FloatLit>(std::numeric_limits<double>::infinity());
    }
    else if (checkKeyword("nan")) {
        advance(); e = std::make_unique<FloatLit>(std::numeric_limits<double>::quiet_NaN());
    }
    else if (checkType(TokenType::IDENTIFIER)) e = std::make_unique<VarExpr>(advance().value);
    else if (matchPunct("(")) {
        e = expression();
        if (!matchPunct(")")) error("expected ')'");
    } else error("expected expression");
    if (e) { e->line = l; e->col = c; }
    return e;
}

static void ind(std::ostringstream& os, int d) { for (int i=0;i<d;i++) os << "  "; }
static void dE(std::ostringstream& os, Expr* e, int d);
static void dS(std::ostringstream& os, Stmt* s, int d);

static void dE(std::ostringstream& os, Expr* e, int d) {
    ind(os, d);
    switch (e->kind) {
        case Expr::Kind::IntLit:    os << "IntLit(" << static_cast<IntLit*>(e)->value << ")\n"; break;
        case Expr::Kind::FloatLit:  os << "FloatLit(" << static_cast<FloatLit*>(e)->value << ")\n"; break;
        case Expr::Kind::StringLit: os << "StringLit(\"" << static_cast<StringLit*>(e)->value << "\")\n"; break;
        case Expr::Kind::Var:       os << "Var(" << static_cast<VarExpr*>(e)->name << ")\n"; break;
        case Expr::Kind::Binary: {
            auto* b = static_cast<BinaryExpr*>(e);
            os << "Binary(" << b->op << ")\n";
            dE(os, b->left.get(), d+1); dE(os, b->right.get(), d+1);
            break;
        }
        case Expr::Kind::Unary: {
            auto* u = static_cast<UnaryExpr*>(e);
            os << "Unary(" << u->op << ")\n"; dE(os, u->operand.get(), d+1);
            break;
        }
        case Expr::Kind::Assign: {
            auto* a = static_cast<AssignExpr*>(e);
            os << "Assign(" << a->name << ")\n"; dE(os, a->value.get(), d+1);
            break;
        }
        case Expr::Kind::AssignIndex: {
            auto* a = static_cast<AssignIndexExpr*>(e);
            os << "AssignIndex(" << a->name << ")\n";
            dE(os, a->index.get(), d+1); dE(os, a->value.get(), d+1);
            break;
        }
        case Expr::Kind::Call: {
            auto* c = static_cast<CallExpr*>(e);
            os << "Call(" << c->name << ")\n";
            for (auto& a : c->args) dE(os, a.get(), d+1);
            break;
        }
        case Expr::Kind::Index: {
            auto* ix = static_cast<IndexExpr*>(e);
            os << "Index(" << ix->name << ")\n"; dE(os, ix->index.get(), d+1);
            break;
        }
    }
}

static void dS(std::ostringstream& os, Stmt* s, int d) {
    ind(os, d);
    switch (s->kind) {
        case Stmt::Kind::VarDecl: {
            auto* v = static_cast<VarDeclStmt*>(s);
            os << "VarDecl(" << v->type << " " << v->name;
            if (v->arraySize) os << "[]";
            os << ")\n";
            if (v->init) dE(os, v->init.get(), d+1);
            if (v->arraySize) { ind(os, d+1); os << "Size:\n"; dE(os, v->arraySize.get(), d+2); }
            for (auto& e : v->arrayInit) dE(os, e.get(), d+1);
            break;
        }
        case Stmt::Kind::ExprStmt: os << "ExprStmt\n"; dE(os, static_cast<ExprStmt*>(s)->expr.get(), d+1); break;
        case Stmt::Kind::Block: {
            os << "Block\n";
            for (auto& st : static_cast<BlockStmt*>(s)->stmts) dS(os, st.get(), d+1);
            break;
        }
        case Stmt::Kind::If: {
            auto* f = static_cast<IfStmt*>(s);
            os << "If\n"; dE(os, f->cond.get(), d+1);
            dS(os, f->thenBranch.get(), d+1);
            if (f->elseBranch) { ind(os, d+1); os << "Else\n"; dS(os, f->elseBranch.get(), d+2); }
            break;
        }
        case Stmt::Kind::While: {
            auto* w = static_cast<WhileStmt*>(s);
            os << "While\n"; dE(os, w->cond.get(), d+1); dS(os, w->body.get(), d+1);
            break;
        }
        case Stmt::Kind::For: {
            auto* f = static_cast<ForStmt*>(s);
            os << "For\n";
            if (f->init) { ind(os, d+1); os << "Init:\n"; dS(os, f->init.get(), d+2); }
            if (f->cond) { ind(os, d+1); os << "Cond:\n"; dE(os, f->cond.get(), d+2); }
            if (f->step) { ind(os, d+1); os << "Step:\n"; dE(os, f->step.get(), d+2); }
            ind(os, d+1); os << "Body:\n"; dS(os, f->body.get(), d+2);
            break;
        }
        case Stmt::Kind::Loop: {
            os << "Loop\n";
            dS(os, static_cast<LoopStmt*>(s)->body.get(), d+1);
            break;
        }
        case Stmt::Kind::DoWhile: {
            auto* dw = static_cast<DoWhileStmt*>(s);
            os << "DoWhile\n";
            ind(os, d+1); os << "Body:\n";
            dS(os, dw->body.get(), d+2);
            ind(os, d+1); os << "Cond:\n";
            dE(os, dw->cond.get(), d+2);
            break;
        }
        case Stmt::Kind::Assert:
            os << "Assert\n";
            dE(os, static_cast<AssertStmt*>(s)->cond.get(), d+1);
            break;
        case Stmt::Kind::Return: os << "Return\n"; if (auto* r = static_cast<ReturnStmt*>(s); r->value) dE(os, r->value.get(), d+1); break;
        case Stmt::Kind::Print: os << "Print\n"; dE(os, static_cast<PrintStmt*>(s)->value.get(), d+1); break;
        case Stmt::Kind::Break: os << "Break\n"; break;
        case Stmt::Kind::Continue: os << "Continue\n"; break;
        case Stmt::Kind::FuncDecl: {
            auto* f = static_cast<FunctionDeclStmt*>(s);
            os << "Func(" << f->returnType << " " << f->name << ")\n";
            for (auto& p : f->params)
                { ind(os, d+1); os << "Param(" << p.type << " " << p.name
                       << (p.isArray ? "[]" : "") << ")\n"; }
            dS(os, f->body.get(), d+1);
            break;
        }
    }
}

std::string Parser::dump(const Program& p) {
    std::ostringstream os;
    os << "Program\n";
    for (auto& s : p.decls) dS(os, s.get(), 1);
    return os.str();
}
