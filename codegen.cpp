#include "codegen.hpp"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Verifier.h"
#include <cstring>
#include <sstream>

using namespace llvm;

std::unique_ptr<Module> Codegen::generate(Program& p, const SemanticAnalyzer& sema,
                                          LLVMContext& ctx) {
    auto mod = std::make_unique<Module>("zeuz", ctx);
    module_ = mod.get();
    ctx_    = &ctx;
    sema_   = &sema;
    error_.clear();

    i64Ty  = llvm::Type::getInt64Ty(ctx);
    i32Ty  = llvm::Type::getInt32Ty(ctx);
    i8Ty   = llvm::Type::getInt8Ty(ctx);
    f64Ty  = llvm::Type::getDoubleTy(ctx);
    voidTy = llvm::Type::getVoidTy(ctx);
    ptrTy  = PointerType::getUnqual(ctx);

    declareExternals();
    declareHelpers();

    {
        auto* scratchFn = Function::Create(
            FunctionType::get(voidTy, false),
            Function::InternalLinkage, "__zz_scratch", module_);
        auto* scratchBB = BasicBlock::Create(ctx, "entry", scratchFn);
        IRBuilder<> scratchBuilder(scratchBB);
        builder_ = &scratchBuilder;
        generateHelperBodies();
        builder_ = nullptr;
        scratchFn->eraseFromParent();
    }
    for (auto& s : p.decls) {
        if (s->kind == Stmt::Kind::FuncDecl) {
            auto* f = static_cast<FunctionDeclStmt*>(s.get());
            if (f->name == "entryf") entryAst_ = f;
            else astFuncs_[f->name] = f;
        }
    }
    for (auto& [name, f] : astFuncs_) {
        std::vector<llvm::Type*> params;
        params.reserve(f->params.size());
        for (auto& p2 : f->params) {
            if (p2.isArray)
                params.push_back(ptrTy);
            else
                params.push_back(llvmType(parseTypeName(p2.type)));
        }
        llvm::Type* retTy = llvmType(parseTypeName(f->returnType));
        auto* fty = FunctionType::get(retTy, params, false);
        auto* fn  = Function::Create(fty, Function::ExternalLinkage, name, module_);
        if (retTy == voidTy) fn->addFnAttr(Attribute::NoUnwind);
        llvmFuncs_[name] = fn;
    }
    for (auto& [name, f] : astFuncs_) {
        if (!generateFunctionBody(f, llvmFuncs_[name])) return nullptr;
        if (!error_.empty()) return nullptr;
    }

    if (!entryAst_) { error_ = "entryf() function not found"; return nullptr; }
    if (!entryAst_->params.empty()) {
        error_ = "entryf() must not take parameters";
        return nullptr;
    }
    auto* mainFn = Function::Create(
        FunctionType::get(i32Ty, {}, false),
        Function::ExternalLinkage, "main", module_);
    mainFn->addFnAttr(Attribute::NoUnwind);

    auto* entryBB = BasicBlock::Create(ctx, "entry", mainFn);
    IRBuilder<> b(entryBB);

    auto* prevFn     = currentFn_;
    auto* prevB      = builder_;
    bool  prevInMain = inMain_;
    currentFn_ = mainFn; builder_ = &b; inMain_ = true;

    pushScope();
    for (auto& s : entryAst_->body->stmts) emitStmt(s.get());
    popScope();

    if (!error_.empty()) return nullptr;
    if (!b.GetInsertBlock()->getTerminator())
        b.CreateRet(b.getInt32(0));

    currentFn_ = prevFn; builder_ = prevB; inMain_ = prevInMain;

    std::string verr;
    raw_string_ostream verrOS(verr);
    if (verifyModule(*module_, &verrOS)) {
        error_ = "LLVM verification failed: " + verr;
        return nullptr;
    }
    return mod;
}

void Codegen::declareExternals() {
    auto* m = module_;

    printfFn   = Function::Create(FunctionType::get(i32Ty, {ptrTy}, true),
                                  Function::ExternalLinkage, "printf", m);
    mallocFn   = Function::Create(FunctionType::get(ptrTy, {i64Ty}, false),
                                  Function::ExternalLinkage, "malloc", m);
    strlenFn   = Function::Create(FunctionType::get(i64Ty, {ptrTy}, false),
                                  Function::ExternalLinkage, "strlen", m);
    strcmpFn   = Function::Create(FunctionType::get(i32Ty, {ptrTy, ptrTy}, false),
                                  Function::ExternalLinkage, "strcmp", m);
    memcpyFn   = Function::Create(FunctionType::get(ptrTy, {ptrTy, ptrTy, i64Ty}, false),
                                  Function::ExternalLinkage, "memcpy", m);
    memsetFn   = Function::Create(FunctionType::get(ptrTy, {ptrTy, i32Ty, i64Ty}, false),
                                  Function::ExternalLinkage, "memset", m);
    snprintfFn = Function::Create(FunctionType::get(i32Ty, {ptrTy, i64Ty, ptrTy}, true),
                                  Function::ExternalLinkage, "snprintf", m);
    fgetsFn    = Function::Create(FunctionType::get(ptrTy, {ptrTy, i32Ty, ptrTy}, false),
                                  Function::ExternalLinkage, "fgets", m);
    fputsFn    = Function::Create(FunctionType::get(i32Ty, {ptrTy, ptrTy}, false),
                                  Function::ExternalLinkage, "fputs", m);
    fflushFn   = Function::Create(FunctionType::get(i32Ty, {ptrTy}, false),
                                  Function::ExternalLinkage, "fflush", m);
    exitFn     = Function::Create(FunctionType::get(voidTy, {i32Ty}, false),
                                  Function::ExternalLinkage, "exit", m);
    exitFn->addFnAttr(Attribute::NoReturn); 

    floorFn    = Function::Create(FunctionType::get(f64Ty, {f64Ty}, false),
                                  Function::ExternalLinkage, "floor", m);

    stdinGV  = new GlobalVariable(*m, ptrTy, false, GlobalValue::ExternalLinkage,
                                  nullptr, "stdin");
    stdoutGV = new GlobalVariable(*m, ptrTy, false, GlobalValue::ExternalLinkage,
                                  nullptr, "stdout");
    stderrGV = new GlobalVariable(*m, ptrTy, false, GlobalValue::ExternalLinkage,
                                  nullptr, "stderr");
}

void Codegen::declareHelpers() {
    zzPrintFloatFn = Function::Create(FunctionType::get(voidTy, {f64Ty}, false),
                                      Function::InternalLinkage, "zz_print_float", module_);
    zzInputFn      = Function::Create(FunctionType::get(ptrTy, {ptrTy}, false),
                                      Function::InternalLinkage, "zz_input", module_);
    zzIntToStrFn   = Function::Create(FunctionType::get(ptrTy, {i64Ty}, false),
                                      Function::InternalLinkage, "zz_int_to_str", module_);
    zzFloatToStrFn = Function::Create(FunctionType::get(ptrTy, {f64Ty}, false),
                                      Function::InternalLinkage, "zz_float_to_str", module_);
    zzConcatFn     = Function::Create(FunctionType::get(ptrTy, {ptrTy, ptrTy}, false),
                                      Function::InternalLinkage, "zz_concat", module_);
}

void Codegen::generatePrintFloatBody() {
    auto* entry = BasicBlock::Create(*ctx_, "entry", zzPrintFloatFn);
    IRBuilder<> b(entry);
    Value* x = zzPrintFloatFn->getArg(0);

    Value* finite = b.CreateFCmpOEQ(b.CreateFSub(x, x), ConstantFP::get(f64Ty, 0.0));
    Value* isInt  = b.CreateFCmpOEQ(x, b.CreateCall(floorFn, {x}));
    Value* both   = b.CreateAnd(finite, isInt);

    auto* intBB   = BasicBlock::Create(*ctx_, "int",   zzPrintFloatFn);
    auto* floatBB = BasicBlock::Create(*ctx_, "float", zzPrintFloatFn);
    auto* endBB   = BasicBlock::Create(*ctx_, "end",   zzPrintFloatFn);
    b.CreateCondBr(both, intBB, floatBB);

    { IRBuilder<> ib(intBB); ib.CreateCall(printfFn, {cstr("%.0f.0\n"), x}); ib.CreateBr(endBB); }
    { IRBuilder<> fb(floatBB); fb.CreateCall(printfFn, {cstr("%g\n"), x}); fb.CreateBr(endBB); }
    IRBuilder<> eb(endBB); eb.CreateRetVoid();
}

void Codegen::generateInputBody() {
    auto* entry = BasicBlock::Create(*ctx_, "entry", zzInputFn);
    IRBuilder<> b(entry);
    Value* prompt = zzInputFn->getArg(0);

    auto* doPromptBB   = BasicBlock::Create(*ctx_, "do_prompt",   zzInputFn);
    auto* afterPromptBB= BasicBlock::Create(*ctx_, "after_prompt",zzInputFn);
    b.CreateCondBr(b.CreateIsNotNull(prompt), doPromptBB, afterPromptBB);
    {
        IRBuilder<> pb(doPromptBB);
        Value* out = pb.CreateLoad(ptrTy, stdoutGV);
        pb.CreateCall(fputsFn,  {prompt, out});
        pb.CreateCall(fflushFn, {out});
        pb.CreateBr(afterPromptBB);
    }
    IRBuilder<> ap(afterPromptBB);

    auto* buf = ap.CreateAlloca(ArrayType::get(i8Ty, 1024));
    Value* bufPtr = buf; 
    Value* stdinV = ap.CreateLoad(ptrTy, stdinGV);
    Value* fgetsR = ap.CreateCall(fgetsFn, {bufPtr, i32Const(1024), stdinV});

    auto* eofBB  = BasicBlock::Create(*ctx_, "eof",  zzInputFn);
    auto* normBB = BasicBlock::Create(*ctx_, "norm", zzInputFn);
    ap.CreateCondBr(ap.CreateIsNull(fgetsR), eofBB, normBB);

    {
        IRBuilder<> eb(eofBB);
        Value* r = eb.CreateCall(mallocFn, {intConst(1)});
        eb.CreateStore(ConstantInt::get(i8Ty, 0), r);
        eb.CreateRet(r);
    }
    {
        IRBuilder<> nb(normBB);
        Value* len        = nb.CreateCall(strlenFn, {bufPtr});
        Value* hasContent = nb.CreateICmpUGT(len, intConst(0));

        auto* checkBB = BasicBlock::Create(*ctx_, "check_nl",     zzInputFn);
        auto* afterBB = BasicBlock::Create(*ctx_, "after_strip",  zzInputFn);
        nb.CreateCondBr(hasContent, checkBB, afterBB);
        {
            IRBuilder<> cb(checkBB);
            Value* lastIdx = cb.CreateSub(len, intConst(1));
            Value* lastPtr = cb.CreateGEP(i8Ty, bufPtr, lastIdx);
            Value* lastCh  = cb.CreateLoad(i8Ty, lastPtr);
            Value* isNl    = cb.CreateICmpEQ(lastCh, ConstantInt::get(i8Ty, '\n'));

            auto* stripBB = BasicBlock::Create(*ctx_, "strip", zzInputFn);
            auto* skipBB  = BasicBlock::Create(*ctx_, "skip",  zzInputFn);
            cb.CreateCondBr(isNl, stripBB, skipBB);
            {
                IRBuilder<> sb(stripBB);
                sb.CreateStore(ConstantInt::get(i8Ty, 0), lastPtr);
                sb.CreateBr(afterBB);
            }
            { IRBuilder<> sk(skipBB); sk.CreateBr(afterBB); }
        }
        IRBuilder<> ab(afterBB);
        Value* newLen = ab.CreateCall(strlenFn, {bufPtr});
        Value* sz     = ab.CreateAdd(newLen, intConst(1));
        Value* r      = ab.CreateCall(mallocFn, {sz});
        ab.CreateCall(memcpyFn, {r, bufPtr, sz});
        ab.CreateRet(r);
    }
}

void Codegen::generateIntToStrBody() {
    auto* entry = BasicBlock::Create(*ctx_, "entry", zzIntToStrFn);
    IRBuilder<> b(entry);
    Value* x   = zzIntToStrFn->getArg(0);
    auto*  buf = b.CreateAlloca(ArrayType::get(i8Ty, 32));
    Value* n   = b.CreateCall(snprintfFn, {buf, intConst(32), cstr("%ld"), x});
    Value* n1  = b.CreateAdd(b.CreateSExt(n, i64Ty), intConst(1));
    Value* r   = b.CreateCall(mallocFn, {n1});
    b.CreateCall(memcpyFn, {r, buf, n1});
    b.CreateRet(r);
}

void Codegen::generateFloatToStrBody() {
    auto* entry = BasicBlock::Create(*ctx_, "entry", zzFloatToStrFn);
    IRBuilder<> b(entry);
    Value* x = zzFloatToStrFn->getArg(0);
    auto* buf     = b.CreateAlloca(ArrayType::get(i8Ty, 64));
    auto* nAlloca = b.CreateAlloca(i32Ty);

    Value* finite = b.CreateFCmpOEQ(b.CreateFSub(x, x), ConstantFP::get(f64Ty, 0.0));
    Value* isInt  = b.CreateFCmpOEQ(x, b.CreateCall(floorFn, {x}));
    Value* both   = b.CreateAnd(finite, isInt);

    auto* intBB   = BasicBlock::Create(*ctx_, "int",   zzFloatToStrFn);
    auto* floatBB = BasicBlock::Create(*ctx_, "float", zzFloatToStrFn);
    auto* afterBB = BasicBlock::Create(*ctx_, "after", zzFloatToStrFn);
    b.CreateCondBr(both, intBB, floatBB);
    {
        IRBuilder<> ib(intBB);
        Value* n = ib.CreateCall(snprintfFn, {buf, intConst(64), cstr("%.0f.0"), x});
        ib.CreateStore(n, nAlloca);
        ib.CreateBr(afterBB);
    }
    {
        IRBuilder<> fb(floatBB);
        Value* n = fb.CreateCall(snprintfFn, {buf, intConst(64), cstr("%g"), x});
        fb.CreateStore(n, nAlloca);
        fb.CreateBr(afterBB);
    }
    IRBuilder<> ab(afterBB);
    Value* n  = ab.CreateLoad(i32Ty, nAlloca);
    Value* n1 = ab.CreateAdd(ab.CreateSExt(n, i64Ty), intConst(1));
    Value* r  = ab.CreateCall(mallocFn, {n1});
    ab.CreateCall(memcpyFn, {r, buf, n1});
    ab.CreateRet(r);
}

void Codegen::generateConcatBody() {
    auto* entry = BasicBlock::Create(*ctx_, "entry", zzConcatFn);
    IRBuilder<> b(entry);
    Value* a  = zzConcatFn->getArg(0);
    Value* bv = zzConcatFn->getArg(1);
    Value* la = b.CreateCall(strlenFn, {a});
    Value* lb = b.CreateCall(strlenFn, {bv});
    Value* total = b.CreateAdd(b.CreateAdd(la, lb), intConst(1));
    Value* r  = b.CreateCall(mallocFn, {total});
    b.CreateCall(memcpyFn, {r, a, la});
    Value* dst = b.CreateGEP(i8Ty, r, la);
    b.CreateCall(memcpyFn, {dst, bv, b.CreateAdd(lb, intConst(1))});
    b.CreateRet(r);
}

void Codegen::generateHelperBodies() {
    generatePrintFloatBody();
    generateInputBody();
    generateIntToStrBody();
    generateFloatToStrBody();
    generateConcatBody();
}

ZType Codegen::parseTypeName(const std::string& s) const {
    if (s == "int" || s == "bool" || s == "char" || s == "byte"
        || s == "i8" || s == "i16" || s == "i32" || s == "i64" || s == "i128"
        || s == "u8" || s == "u16" || s == "u32" || s == "u64" || s == "u128"
        || s == "usize" || s == "isize")
        return ZType::Int;
    if (s == "float" || s == "f16" || s == "f32" || s == "f64")
        return ZType::Float;
    if (s == "string") return ZType::String;
    if (s == "void")   return ZType::Void;
    return ZType::Error;
}

ZType Codegen::typeOf(Expr* e) const {
    auto& m = sema_->exprTypes();
    auto it = m.find(e);
    return it == m.end() ? ZType::Error : it->second;
}

llvm::Type* Codegen::llvmType(ZType t) const {
    switch (t) {
        case ZType::Int:    return i64Ty;
        case ZType::Float:  return f64Ty;
        case ZType::String: return ptrTy;
        case ZType::Void:   return voidTy;
        default:            return i64Ty;
    }
}

Value* Codegen::cstr(const std::string& s) {
    return builder_->CreateGlobalString(s, ".str");
}
Value* Codegen::intConst(long v)      { return ConstantInt::get(i64Ty, v, true); }
Value* Codegen::i32Const(int v)       { return ConstantInt::get(i32Ty, v, true); }
Value* Codegen::doubleConst(double v) { return ConstantFP::get(f64Ty, v); }

Codegen::VarSlot* Codegen::lookupVar(const std::string& n) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->find(n);
        if (f != it->end()) return &f->second;
    }
    return nullptr;
}

bool Codegen::generateFunctionBody(FunctionDeclStmt* f, Function* fn) {
    auto* entry = BasicBlock::Create(*ctx_, "entry", fn);
    IRBuilder<> b(entry);

    auto* prevFn     = currentFn_;
    auto* prevB      = builder_;
    bool  prevInMain = inMain_;
    currentFn_ = fn; builder_ = &b; inMain_ = false;

    pushScope();

    size_t i = 0;
    for (auto& param : f->params) {
        ZType pt = parseTypeName(param.type);
        if (param.isArray) {
            Value* arg = fn->getArg(i++);
            scopes_.back()[param.name] = VarSlot{arg, pt, true, nullptr};
        } else {
            auto* a = b.CreateAlloca(llvmType(pt), nullptr, param.name);
            b.CreateStore(fn->getArg(i++), a);
            scopes_.back()[param.name] = VarSlot{a, pt, false, nullptr};
        }
    }

    for (auto& s : f->body->stmts) emitStmt(s.get());

    if (!b.GetInsertBlock()->getTerminator()) {
        ZType rt = parseTypeName(f->returnType);
        if      (rt == ZType::Void)   b.CreateRetVoid();
        else if (rt == ZType::String) b.CreateRet(ConstantPointerNull::get(ptrTy));
        else if (rt == ZType::Float)  b.CreateRet(doubleConst(0.0));
        else                          b.CreateRet(intConst(0));
    }

    popScope();
    currentFn_ = prevFn; builder_ = prevB; inMain_ = prevInMain;
    return true;
}

void Codegen::emitStmt(Stmt* s) {
    switch (s->kind) {
        case Stmt::Kind::VarDecl:   emitVarDecl(static_cast<VarDeclStmt*>(s)); break;
        case Stmt::Kind::ExprStmt:  emitExpr(static_cast<ExprStmt*>(s)->expr.get()); break;

        case Stmt::Kind::Block: {
            pushScope();
            for (auto& st : static_cast<BlockStmt*>(s)->stmts) emitStmt(st.get());
            popScope();
            break;
        }
        case Stmt::Kind::If:      emitIf(static_cast<IfStmt*>(s)); break;
        case Stmt::Kind::While:   emitWhile(static_cast<WhileStmt*>(s)); break;
        case Stmt::Kind::For:     emitFor(static_cast<ForStmt*>(s)); break;
        case Stmt::Kind::Loop:    emitLoop(static_cast<LoopStmt*>(s)); break;
        case Stmt::Kind::DoWhile: emitDoWhile(static_cast<DoWhileStmt*>(s)); break;
        case Stmt::Kind::Assert:  emitAssert(static_cast<AssertStmt*>(s)); break;
        case Stmt::Kind::Return:  emitReturn(static_cast<ReturnStmt*>(s)); break;
        case Stmt::Kind::Print:   emitPrint(static_cast<PrintStmt*>(s)); break;

        case Stmt::Kind::Break:
            if (!breakTargets_.empty()) builder_->CreateBr(breakTargets_.back());
            break;
        case Stmt::Kind::Continue:
            if (!continueTargets_.empty()) builder_->CreateBr(continueTargets_.back());
            break;
        case Stmt::Kind::FuncDecl: break;
    }
}

void Codegen::emitBody(Stmt* body) {
    if (body->kind == Stmt::Kind::Block) {
        pushScope();
        for (auto& st : static_cast<BlockStmt*>(body)->stmts) emitStmt(st.get());
        popScope();
    } else {
        emitStmt(body);
    }
}

void Codegen::emitVarDecl(VarDeclStmt* v) {
    ZType t = parseTypeName(v->type);

    if (v->arraySize) {
        Value* nVal;
        if (v->arraySize->kind == Expr::Kind::IntLit) {
            nVal = intConst(static_cast<IntLit*>(v->arraySize.get())->value);
        } else {
            nVal = emitExpr(v->arraySize.get());
            Value* neg  = builder_->CreateICmpSLT(nVal, intConst(0));
            auto* errBB = BasicBlock::Create(*ctx_, "arr_err", currentFn_);
            auto* okBB  = BasicBlock::Create(*ctx_, "arr_ok",  currentFn_);
            builder_->CreateCondBr(neg, errBB, okBB);
            {
                IRBuilder<> eb(errBB);
                Value* err = eb.CreateLoad(ptrTy, stderrGV);
                eb.CreateCall(fputsFn, {cstr("negative array size\n"), err});
                eb.CreateCall(exitFn,  {i32Const(1)});
                eb.CreateUnreachable();
            }
            builder_->SetInsertPoint(okBB);
        }

        llvm::Type* lt = llvmType(t);
        auto* arr = builder_->CreateAlloca(lt, nVal, v->name);
        builder_->CreateCall(memsetFn,
            {arr, i32Const(0), builder_->CreateMul(nVal, intConst(8))});

        scopes_.back()[v->name] = VarSlot{arr, t, true, nVal};

        for (size_t i = 0; i < v->arrayInit.size(); i++) {
            Value* ev = emitExpr(v->arrayInit[i].get());
            Value* ep = builder_->CreateGEP(lt, arr, intConst((long)i));
            builder_->CreateStore(ev, ep);
        }
    } else {
        auto* a = builder_->CreateAlloca(llvmType(t), nullptr, v->name);
        Value* init;
        if      (v->init)          init = emitExpr(v->init.get());
        else if (t == ZType::Float) init = doubleConst(0.0);
        else if (t == ZType::String)init = cstr("");
        else                        init = intConst(0);
        builder_->CreateStore(init, a);
        scopes_.back()[v->name] = VarSlot{a, t, false, nullptr};
    }
}

void Codegen::emitIf(IfStmt* s) {
    Value* cond = emitTruthy(s->cond.get());
    auto* thenBB  = BasicBlock::Create(*ctx_, "then",  currentFn_);
    auto* mergeBB = BasicBlock::Create(*ctx_, "merge", currentFn_);
    BasicBlock* elseBB = s->elseBranch
        ? BasicBlock::Create(*ctx_, "else", currentFn_)
        : nullptr;

    builder_->CreateCondBr(cond, thenBB, elseBB ? elseBB : mergeBB);

    builder_->SetInsertPoint(thenBB);
    emitBody(s->thenBranch.get());
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(mergeBB);

    if (elseBB) {
        builder_->SetInsertPoint(elseBB);
        emitBody(s->elseBranch.get());
        if (!builder_->GetInsertBlock()->getTerminator())
            builder_->CreateBr(mergeBB);
    }
    builder_->SetInsertPoint(mergeBB);
}

void Codegen::emitWhile(WhileStmt* s) {
    auto* condBB = BasicBlock::Create(*ctx_, "while_cond", currentFn_);
    auto* bodyBB = BasicBlock::Create(*ctx_, "while_body", currentFn_);
    auto* endBB  = BasicBlock::Create(*ctx_, "while_end",  currentFn_);

    builder_->CreateBr(condBB);
    builder_->SetInsertPoint(condBB);
    builder_->CreateCondBr(emitTruthy(s->cond.get()), bodyBB, endBB);

    builder_->SetInsertPoint(bodyBB);
    breakTargets_.push_back(endBB);
    continueTargets_.push_back(condBB);
    emitBody(s->body.get());
    breakTargets_.pop_back();
    continueTargets_.pop_back();
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(condBB);

    builder_->SetInsertPoint(endBB);
}

void Codegen::emitFor(ForStmt* s) {
    auto* condBB = BasicBlock::Create(*ctx_, "for_cond", currentFn_);
    auto* bodyBB = BasicBlock::Create(*ctx_, "for_body", currentFn_);
    auto* stepBB = BasicBlock::Create(*ctx_, "for_step", currentFn_);
    auto* endBB  = BasicBlock::Create(*ctx_, "for_end",  currentFn_);

    pushScope();
    if (s->init) emitStmt(s->init.get());
    builder_->CreateBr(condBB);

    builder_->SetInsertPoint(condBB);
    if (s->cond) builder_->CreateCondBr(emitTruthy(s->cond.get()), bodyBB, endBB);
    else         builder_->CreateBr(bodyBB);

    builder_->SetInsertPoint(bodyBB);
    breakTargets_.push_back(endBB);
    continueTargets_.push_back(stepBB);
    emitBody(s->body.get());
    breakTargets_.pop_back();
    continueTargets_.pop_back();
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(stepBB);

    builder_->SetInsertPoint(stepBB);
    if (s->step) emitExpr(s->step.get());
    builder_->CreateBr(condBB);

    builder_->SetInsertPoint(endBB);
    popScope();
}

void Codegen::emitLoop(LoopStmt* s) {
    auto* bodyBB = BasicBlock::Create(*ctx_, "loop_body", currentFn_);
    auto* endBB  = BasicBlock::Create(*ctx_, "loop_end",  currentFn_);
    builder_->CreateBr(bodyBB);
    builder_->SetInsertPoint(bodyBB);
    breakTargets_.push_back(endBB);
    continueTargets_.push_back(bodyBB);
    emitBody(s->body.get());
    breakTargets_.pop_back();
    continueTargets_.pop_back();
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(bodyBB);
    builder_->SetInsertPoint(endBB);
}

void Codegen::emitDoWhile(DoWhileStmt* s) {
    auto* bodyBB = BasicBlock::Create(*ctx_, "do_body", currentFn_);
    auto* condBB = BasicBlock::Create(*ctx_, "do_cond", currentFn_);
    auto* endBB  = BasicBlock::Create(*ctx_, "do_end",  currentFn_);
    builder_->CreateBr(bodyBB);
    builder_->SetInsertPoint(bodyBB);
    breakTargets_.push_back(endBB);
    continueTargets_.push_back(condBB);
    emitBody(s->body.get());
    breakTargets_.pop_back();
    continueTargets_.pop_back();
    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(condBB);
    builder_->SetInsertPoint(condBB);
    builder_->CreateCondBr(emitTruthy(s->cond.get()), bodyBB, endBB);
    builder_->SetInsertPoint(endBB);
}

void Codegen::emitAssert(AssertStmt* s) {
    Value* ok = emitTruthy(s->cond.get());
    auto* okBB   = BasicBlock::Create(*ctx_, "asrt_ok",   currentFn_);
    auto* failBB = BasicBlock::Create(*ctx_, "asrt_fail", currentFn_);
    builder_->CreateCondBr(ok, okBB, failBB);
    {
        IRBuilder<> fb(failBB);
        Value* err = fb.CreateLoad(ptrTy, stderrGV);
        std::string msg = "assertion failed at line " + std::to_string(s->line) + "\n";
        fb.CreateCall(fputsFn, {cstr(msg), err});
        fb.CreateCall(exitFn,  {i32Const(1)});
        fb.CreateUnreachable();
    }
    builder_->SetInsertPoint(okBB);
}

void Codegen::emitReturn(ReturnStmt* s) {
    if (inMain_) {
        if (s->value) {
            ZType t = typeOf(s->value.get());
            Value* v = emitExpr(s->value.get());
            Value* exitCode;
            if      (t == ZType::Float)  exitCode = builder_->CreateFPToSI(v, i32Ty);
            else if (t == ZType::String) exitCode = i32Const(0);
            else                         exitCode = builder_->CreateTrunc(v, i32Ty);
            builder_->CreateRet(exitCode);
        } else {
            builder_->CreateRet(i32Const(0));
        }
        return;
    }
    if (s->value) builder_->CreateRet(emitExpr(s->value.get()));
    else          builder_->CreateRetVoid();
}

void Codegen::emitPrint(PrintStmt* s) {
    Expr* e = s->value.get();
    if (e->kind == Expr::Kind::Var) {
        auto* v = static_cast<VarExpr*>(e);
        if (auto* slot = lookupVar(v->name); slot && slot->isArray) {
            emitPrintArray(*slot);
            return;
        }
    }
    emitPrintValueNewline(emitExpr(e), typeOf(e));
}

void Codegen::emitPrintValueNoNewline(Value* v, ZType t) {
    if (t == ZType::Int)
        builder_->CreateCall(printfFn, {cstr("%ld"), v});
    else if (t == ZType::String)
        builder_->CreateCall(printfFn, {cstr("%s"), v});
    else if (t == ZType::Float) {
        Value* finite = builder_->CreateFCmpOEQ(builder_->CreateFSub(v, v),
                                                ConstantFP::get(f64Ty, 0.0));
        Value* isInt  = builder_->CreateFCmpOEQ(v, builder_->CreateCall(floorFn, {v}));
        Value* both   = builder_->CreateAnd(finite, isInt);
        auto* iBB = BasicBlock::Create(*ctx_, "pf_i", currentFn_);
        auto* fBB = BasicBlock::Create(*ctx_, "pf_f", currentFn_);
        auto* mBB = BasicBlock::Create(*ctx_, "pf_m", currentFn_);
        builder_->CreateCondBr(both, iBB, fBB);
        { IRBuilder<> ib(iBB); ib.CreateCall(printfFn, {cstr("%.0f.0"), v}); ib.CreateBr(mBB); }
        { IRBuilder<> fb(fBB); fb.CreateCall(printfFn, {cstr("%g"), v});     fb.CreateBr(mBB); }
        builder_->SetInsertPoint(mBB);
    }
}

void Codegen::emitPrintValueNewline(Value* v, ZType t) {
    if      (t == ZType::Int)    builder_->CreateCall(printfFn, {cstr("%ld\n"), v});
    else if (t == ZType::String) builder_->CreateCall(printfFn, {cstr("%s\n"), v});
    else if (t == ZType::Float)  builder_->CreateCall(zzPrintFloatFn, {v});
}

void Codegen::emitPrintArray(VarSlot& slot) {
    if (!slot.arrayLen) {
        error_ = "cannot print array parameter; iterate with a loop";
        return;
    }
    Value* len = slot.arrayLen;
    llvm::Type* lt = llvmType(slot.kind);

    builder_->CreateCall(printfFn, {cstr("[")});

    auto* idxA = builder_->CreateAlloca(i64Ty);
    builder_->CreateStore(intConst(0), idxA);

    auto* condBB = BasicBlock::Create(*ctx_, "pa_cond", currentFn_);
    auto* bodyBB = BasicBlock::Create(*ctx_, "pa_body", currentFn_);
    auto* endBB  = BasicBlock::Create(*ctx_, "pa_end",  currentFn_);
    builder_->CreateBr(condBB);

    builder_->SetInsertPoint(condBB);
    Value* idx = builder_->CreateLoad(i64Ty, idxA);
    builder_->CreateCondBr(builder_->CreateICmpSLT(idx, len), bodyBB, endBB);

    builder_->SetInsertPoint(bodyBB);
    Value* curIdx  = builder_->CreateLoad(i64Ty, idxA);
    Value* needSep = builder_->CreateICmpSGT(curIdx, intConst(0));
    auto* sepBB  = BasicBlock::Create(*ctx_, "pa_sep",  currentFn_);
    auto* elemBB = BasicBlock::Create(*ctx_, "pa_elem", currentFn_);
    builder_->CreateCondBr(needSep, sepBB, elemBB);
    {
        IRBuilder<> sb(sepBB);
        sb.CreateCall(printfFn, {cstr(", ")});
        sb.CreateBr(elemBB);
    }
    builder_->SetInsertPoint(elemBB);
    Value* elemPtr = builder_->CreateGEP(lt, slot.alloca, curIdx);
    Value* elem    = builder_->CreateLoad(lt, elemPtr);
    emitPrintValueNoNewline(elem, slot.kind);
    builder_->CreateStore(builder_->CreateAdd(curIdx, intConst(1)), idxA);
    builder_->CreateBr(condBB);

    builder_->SetInsertPoint(endBB);
    builder_->CreateCall(printfFn, {cstr("]\n")});
}

Value* Codegen::emitExpr(Expr* e) {
    switch (e->kind) {
        case Expr::Kind::IntLit:
            return intConst(static_cast<IntLit*>(e)->value);
        case Expr::Kind::FloatLit:
            return doubleConst(static_cast<FloatLit*>(e)->value);
        case Expr::Kind::StringLit:
            return cstr(static_cast<StringLit*>(e)->value);

        case Expr::Kind::Var: {
            auto* v = static_cast<VarExpr*>(e);
            auto* slot = lookupVar(v->name);
            if (!slot) { error_ = "undefined variable: " + v->name; return intConst(0); }
            if (slot->isArray) {
                error_ = "array '" + v->name + "' used in scalar context";
                return intConst(0);
            }
            return builder_->CreateLoad(llvmType(slot->kind), slot->alloca);
        }
        case Expr::Kind::Binary:      return emitBinary(static_cast<BinaryExpr*>(e));
        case Expr::Kind::Unary:       return emitUnary(static_cast<UnaryExpr*>(e));
        case Expr::Kind::Assign:      return emitAssign(static_cast<AssignExpr*>(e));
        case Expr::Kind::AssignIndex: return emitAssignIndex(static_cast<AssignIndexExpr*>(e));
        case Expr::Kind::Call:        return emitCall(static_cast<CallExpr*>(e));
        case Expr::Kind::Index:       return emitIndex(static_cast<IndexExpr*>(e));
    }
    return intConst(0);
}

Value* Codegen::emitTruthy(Expr* e) {
    if (e->kind == Expr::Kind::Var) {
        auto* v = static_cast<VarExpr*>(e);
        if (auto* slot = lookupVar(v->name); slot && slot->isArray) {
            if (!slot->arrayLen)
                return ConstantInt::getTrue(*ctx_);   // array param: anggap truthy
            return builder_->CreateICmpSGT(slot->arrayLen, intConst(0));
        }
    }
    ZType t = typeOf(e);
    Value* v = emitExpr(e);
    if (t == ZType::String)
        return builder_->CreateICmpNE(builder_->CreateCall(strlenFn, {v}), intConst(0));
    if (t == ZType::Float)
        return builder_->CreateFCmpUNE(v, ConstantFP::get(f64Ty, 0.0));
    return builder_->CreateICmpNE(v, intConst(0));
}

Value* Codegen::emitToString(Value* v, ZType t) {
    if (t == ZType::String) return v;
    if (t == ZType::Int)    return builder_->CreateCall(zzIntToStrFn, {v});
    return builder_->CreateCall(zzFloatToStrFn, {v});
}

Value* Codegen::emitShortCircuit(BinaryExpr* b) {
    bool isAnd = (b->op == "&&");
    Value* left = emitTruthy(b->left.get());

    auto* rightBB = BasicBlock::Create(*ctx_, "sc_right", currentFn_);
    auto* shortBB = BasicBlock::Create(*ctx_, "sc_short", currentFn_);
    auto* mergeBB = BasicBlock::Create(*ctx_, "sc_merge", currentFn_);

    if (isAnd) builder_->CreateCondBr(left, rightBB, shortBB);
    else       builder_->CreateCondBr(left, shortBB, rightBB);

    builder_->SetInsertPoint(shortBB);
    builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(rightBB);
    Value* rtruth = emitTruthy(b->right.get());
    BasicBlock* rightEnd = builder_->GetInsertBlock();
    Value* rint = builder_->CreateZExt(rtruth, i64Ty);
    builder_->CreateBr(mergeBB);

    builder_->SetInsertPoint(mergeBB);
    auto* phi = builder_->CreatePHI(i64Ty, 2);
    phi->addIncoming(intConst(isAnd ? 0 : 1), shortBB);
    phi->addIncoming(rint, rightEnd);
    return phi;
}

Value* Codegen::emitBinary(BinaryExpr* b) {
    if (b->op == "&&" || b->op == "||") return emitShortCircuit(b);

    ZType lt  = typeOf(b->left.get());
    ZType rt  = typeOf(b->right.get());

    if (b->op == "+" && (lt == ZType::String || rt == ZType::String)) {
        Value* l = emitExpr(b->left.get());
        Value* r = emitExpr(b->right.get());
        return builder_->CreateCall(zzConcatFn, {emitToString(l, lt), emitToString(r, rt)});
    }

    Value* l = emitExpr(b->left.get());
    Value* r = emitExpr(b->right.get());

    if (lt == ZType::String && rt == ZType::String) {
        Value* cmp  = builder_->CreateCall(strcmpFn, {l, r});
        Value* zero = i32Const(0);
        Value* c;
        if      (b->op == "==") c = builder_->CreateICmpEQ (cmp, zero);
        else if (b->op == "!=") c = builder_->CreateICmpNE (cmp, zero);
        else if (b->op == "<")  c = builder_->CreateICmpSLT(cmp, zero);
        else if (b->op == ">")  c = builder_->CreateICmpSGT(cmp, zero);
        else if (b->op == "<=") c = builder_->CreateICmpSLE(cmp, zero);
        else if (b->op == ">=") c = builder_->CreateICmpSGE(cmp, zero);
        else { error_ = "unsupported string op: " + b->op; return intConst(0); }
        return builder_->CreateZExt(c, i64Ty);
    }

    bool usef = (lt == ZType::Float || rt == ZType::Float);
    if (usef && lt != ZType::Float) l = builder_->CreateSIToFP(l, f64Ty);
    if (usef && rt != ZType::Float) r = builder_->CreateSIToFP(r, f64Ty);

    auto toBool = [&](Value* v) { return builder_->CreateZExt(v, i64Ty); };

    if (b->op == "==") return toBool(usef ? builder_->CreateFCmpOEQ(l, r) : builder_->CreateICmpEQ (l, r));
    if (b->op == "!=") return toBool(usef ? builder_->CreateFCmpONE(l, r) : builder_->CreateICmpNE (l, r));
    if (b->op == "<")  return toBool(usef ? builder_->CreateFCmpOLT(l, r) : builder_->CreateICmpSLT(l, r));
    if (b->op == ">")  return toBool(usef ? builder_->CreateFCmpOGT(l, r) : builder_->CreateICmpSGT(l, r));
    if (b->op == "<=") return toBool(usef ? builder_->CreateFCmpOLE(l, r) : builder_->CreateICmpSLE(l, r));
    if (b->op == ">=") return toBool(usef ? builder_->CreateFCmpOGE(l, r) : builder_->CreateICmpSGE(l, r));

    if (b->op == "+") return usef ? builder_->CreateFAdd(l, r) : builder_->CreateAdd(l, r);
    if (b->op == "-") return usef ? builder_->CreateFSub(l, r) : builder_->CreateSub(l, r);
    if (b->op == "*") return usef ? builder_->CreateFMul(l, r) : builder_->CreateMul(l, r);

    if (b->op == "/") {
        Value* zero   = usef ? ConstantFP::get(f64Ty, 0.0) : intConst(0);
        Value* isZero = usef ? builder_->CreateFCmpOEQ(r, zero)
                             : builder_->CreateICmpEQ(r, zero);
        auto* errBB = BasicBlock::Create(*ctx_, "div_err", currentFn_);
        auto* okBB  = BasicBlock::Create(*ctx_, "div_ok",  currentFn_);
        builder_->CreateCondBr(isZero, errBB, okBB);
        {
            IRBuilder<> eb(errBB);
            Value* e = eb.CreateLoad(ptrTy, stderrGV);
            eb.CreateCall(fputsFn, {cstr("division by zero\n"), e});
            eb.CreateCall(exitFn,  {i32Const(1)});
            eb.CreateUnreachable();
        }
        builder_->SetInsertPoint(okBB);
        return usef ? builder_->CreateFDiv(l, r) : builder_->CreateSDiv(l, r);
    }

    error_ = "unsupported op: " + b->op;
    return intConst(0);
}

Value* Codegen::emitUnary(UnaryExpr* u) {
    if (u->op.rfind("cast_", 0) == 0) {
        ZType targetT = parseTypeName(u->op.substr(5));
        ZType srcT    = typeOf(u->operand.get());
        Value* v = emitExpr(u->operand.get());
        if (targetT == ZType::Int && srcT == ZType::Float)
            return builder_->CreateFPToSI(v, i64Ty);
        if (targetT == ZType::Float && srcT == ZType::Int)
            return builder_->CreateSIToFP(v, f64Ty);
        if (targetT == ZType::String && srcT == ZType::Int)
            return builder_->CreateCall(zzIntToStrFn, {v});
        if (targetT == ZType::String && srcT == ZType::Float)
            return builder_->CreateCall(zzFloatToStrFn, {v});
        return v;
    }

    Value* v = emitExpr(u->operand.get());
    if (u->op == "-") {
        ZType t = typeOf(u->operand.get());
        return (t == ZType::Float) ? builder_->CreateFNeg(v) : builder_->CreateNeg(v);
    }
    if (u->op == "!") {
        Value* truthy = emitTruthy(u->operand.get());
        return builder_->CreateZExt(builder_->CreateNot(truthy), i64Ty);
    }
    return v;
}

Value* Codegen::emitAssign(AssignExpr* a) {
    auto* slot = lookupVar(a->name);
    if (!slot) { error_ = "undefined variable: " + a->name; return intConst(0); }
    if (slot->isArray) {
        error_ = "cannot assign to array '" + a->name + "' as a whole";
        return intConst(0);
    }
    ZType vt = typeOf(a->value.get());
    Value* v = emitExpr(a->value.get());
    if (slot->kind == ZType::Float && vt == ZType::Int)
        v = builder_->CreateSIToFP(v, f64Ty);
    builder_->CreateStore(v, slot->alloca);
    return v;
}

Value* Codegen::emitAssignIndex(AssignIndexExpr* a) {
    auto* slot = lookupVar(a->name);
    if (!slot) { error_ = "undefined array: " + a->name; return intConst(0); }
    if (!slot->isArray) {
        error_ = "'" + a->name + "' is not an array";
        return intConst(0);
    }
    Value* idx = emitExpr(a->index.get());
    Value* v   = emitExpr(a->value.get());
    llvm::Type* lt = llvmType(slot->kind);
    Value* ptr = builder_->CreateGEP(lt, slot->alloca, idx);
    builder_->CreateStore(v, ptr);
    return v;
}

Value* Codegen::emitIndex(IndexExpr* ix) {
    auto* slot = lookupVar(ix->name);
    if (!slot) { error_ = "undefined array: " + ix->name; return intConst(0); }
    if (!slot->isArray) {
        error_ = "'" + ix->name + "' is not an array";
        return intConst(0);
    }
    Value* idx = emitExpr(ix->index.get());
    llvm::Type* lt = llvmType(slot->kind);
    return builder_->CreateLoad(lt, builder_->CreateGEP(lt, slot->alloca, idx));
}

Value* Codegen::emitCall(CallExpr* c) {
    if (c->name == "len") {
        Expr* arg = c->args[0].get();
        if (arg->kind == Expr::Kind::Var) {
            auto* v = static_cast<VarExpr*>(arg);
            if (auto* slot = lookupVar(v->name); slot && slot->isArray) {
                if (!slot->arrayLen) {
                    error_ = "len() on array parameter requires explicit length";
                    return intConst(0);
                }
                return slot->arrayLen;
            }
        }
        ZType at = typeOf(arg);
        if (at == ZType::String)
            return builder_->CreateCall(strlenFn, {emitExpr(arg)});
        error_ = "len() argument must be array or string";
        return intConst(0);
    }
    if (c->name == "input") {
        return builder_->CreateCall(zzInputFn, {emitExpr(c->args[0].get())});
    }

    auto it = llvmFuncs_.find(c->name);
    if (it == llvmFuncs_.end()) {
        error_ = "undefined function: " + c->name;
        return intConst(0);
    }

    auto* ast = astFuncs_[c->name];
    std::vector<Value*> args;
    args.reserve(c->args.size());
    for (size_t i = 0; i < c->args.size(); i++) {
        auto& astParam = ast->params[i];
        if (astParam.isArray) {
            Expr* arg = c->args[i].get();
            if (arg->kind != Expr::Kind::Var) {
                error_ = "array argument must be a variable";
                return intConst(0);
            }
            auto* v = static_cast<VarExpr*>(arg);
            auto* slot = lookupVar(v->name);
            if (!slot || !slot->isArray) {
                error_ = "'" + v->name + "' is not an array";
                return intConst(0);
            }
            args.push_back(slot->alloca);
        } else {
            ZType pt = parseTypeName(astParam.type);
            ZType at = typeOf(c->args[i].get());
            Value* a = emitExpr(c->args[i].get());
            if (pt == ZType::Float && at == ZType::Int)
                a = builder_->CreateSIToFP(a, f64Ty);
            args.push_back(a);
        }
    }
    return builder_->CreateCall(it->second, args);
}
