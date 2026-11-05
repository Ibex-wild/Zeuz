#ifndef CODEGEN_HPP
#define CODEGEN_HPP
#include "semantic.hpp"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <string>
#include <unordered_map>
#include <vector>

using ZType = ::Type;

class Codegen {
public:
    std::unique_ptr<llvm::Module> generate(Program& p, const SemanticAnalyzer& sema,
                                           llvm::LLVMContext& ctx);
    const std::string& error() const { return error_; }

private:
    struct VarSlot {
        llvm::Value* alloca = nullptr;
        ZType kind = ZType::Int;
        bool isArray = false;
        llvm::Value* arrayLen = nullptr;
    };

    std::string error_;
    llvm::Module* module_ = nullptr;
    llvm::LLVMContext* ctx_ = nullptr;
    const SemanticAnalyzer* sema_ = nullptr;
    llvm::IRBuilder<>* builder_ = nullptr;
    llvm::Function* currentFn_ = nullptr;
    bool inMain_ = false;

    std::vector<std::unordered_map<std::string, VarSlot>> scopes_;
    std::vector<llvm::BasicBlock*> breakTargets_;
    std::vector<llvm::BasicBlock*> continueTargets_;

    llvm::IntegerType* i64Ty  = nullptr;
    llvm::IntegerType* i32Ty  = nullptr;
    llvm::IntegerType* i8Ty   = nullptr;
    llvm::Type* f64Ty         = nullptr;
    llvm::Type* voidTy        = nullptr;
    llvm::PointerType* ptrTy  = nullptr;

    llvm::Function *printfFn = nullptr, *mallocFn = nullptr, *strlenFn = nullptr,
                   *strcmpFn = nullptr, *memcpyFn = nullptr, *memsetFn = nullptr,
                   *snprintfFn = nullptr, *fgetsFn = nullptr, *fputsFn = nullptr,
                   *fflushFn = nullptr, *exitFn = nullptr, *floorFn = nullptr;
    llvm::GlobalVariable *stdinGV = nullptr, *stdoutGV = nullptr, *stderrGV = nullptr;

    llvm::Function *zzPrintFloatFn = nullptr, *zzInputFn = nullptr,
                   *zzIntToStrFn = nullptr, *zzFloatToStrFn = nullptr,
                   *zzConcatFn = nullptr;

    std::unordered_map<std::string, FunctionDeclStmt*> astFuncs_;
    std::unordered_map<std::string, llvm::Function*> llvmFuncs_;
    FunctionDeclStmt* entryAst_ = nullptr;

    void declareExternals();
    void declareHelpers();
    void generateHelperBodies();
    void generatePrintFloatBody();
    void generateInputBody();
    void generateIntToStrBody();
    void generateFloatToStrBody();
    void generateConcatBody();
    bool generateFunctionBody(FunctionDeclStmt* f, llvm::Function* fn);

    void pushScope() { scopes_.emplace_back(); }
    void popScope() { scopes_.pop_back(); }
    VarSlot* lookupVar(const std::string& name);

    llvm::Type* llvmType(ZType t) const;
    ZType parseTypeName(const std::string& s) const;
    ZType typeOf(Expr* e) const;
    llvm::Value* cstr(const std::string& s);
    llvm::Value* intConst(long v);
    llvm::Value* i32Const(int v);
    llvm::Value* doubleConst(double v);

    void emitStmt(Stmt* s);
    void emitBody(Stmt* body);
    void emitVarDecl(VarDeclStmt* v);
    void emitIf(IfStmt* s);
    void emitWhile(WhileStmt* s);
    void emitFor(ForStmt* s);
    void emitLoop(LoopStmt* s);
    void emitDoWhile(DoWhileStmt* s);
    void emitAssert(AssertStmt* s);
    void emitReturn(ReturnStmt* s);
    void emitPrint(PrintStmt* s);
    void emitPrintValueNewline(llvm::Value* v, ZType t);
    void emitPrintValueNoNewline(llvm::Value* v, ZType t);
    void emitPrintArray(VarSlot& slot);

    llvm::Value* emitExpr(Expr* e);
    llvm::Value* emitBinary(BinaryExpr* b);
    llvm::Value* emitShortCircuit(BinaryExpr* b);
    llvm::Value* emitUnary(UnaryExpr* u);
    llvm::Value* emitAssign(AssignExpr* a);
    llvm::Value* emitAssignIndex(AssignIndexExpr* a);
    llvm::Value* emitCall(CallExpr* c);
    llvm::Value* emitIndex(IndexExpr* e);
    llvm::Value* emitTruthy(Expr* e);
    llvm::Value* emitToString(llvm::Value* v, ZType t);
};
#endif
