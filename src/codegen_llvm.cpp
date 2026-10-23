#include "codegen_llvm.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include <unordered_map>

using namespace llvm;

namespace {

class IRGen {
public:
  IRGen(LLVMContext &ctx, Module &mod) : C(ctx), M(mod), B(ctx) {}

  void program(const Program &p) {
    // Declare every function first so calls can be emitted in any order.
    for (auto &item : p.items)
      if (item.function) declareFunction(*item.function);
    for (auto &item : p.items) {
      if (item.global) global(*item.global);
      else if (!item.function->isExtern()) function(*item.function);
    }
  }

private:
  LLVMContext &C;
  Module &M;
  IRBuilder<> B;
  Function *fn = nullptr;
  std::unordered_map<const VarDecl *, Value *> storage;

  llvm::Type *ty(::Type t) {
    switch (t) {
    case ::Type::Void: return B.getVoidTy();
    case ::Type::Bool: return B.getInt1Ty();
    case ::Type::Int: return B.getInt32Ty();
    case ::Type::Float: return B.getFloatTy();
    }
    return nullptr;
  }

  void declareFunction(const FunctionDecl &f) {
    if (M.getFunction(f.name)) return;
    std::vector<llvm::Type *> params;
    for (auto &p : f.params) params.push_back(ty(p->type));
    auto *ft = FunctionType::get(ty(f.returnType), params, false);
    Function *F = Function::Create(ft, Function::ExternalLinkage, f.name, M);
    unsigned i = 0;
    for (auto &arg : F->args()) arg.setName(f.params[i++]->name);
  }

  void global(const VarDecl &v) {
    // Common linkage + zero initialiser, which is what clang emits for `int g;` in C.
    auto *g = new GlobalVariable(M, ty(v.type), false, GlobalValue::CommonLinkage,
                                 Constant::getNullValue(ty(v.type)), v.name);
    g->setAlignment(Align(4));
    storage[&v] = g;
  }

  // Every local lives in an entry-block alloca; mem2reg/SROA later promote them
  // to SSA registers and insert the phi nodes for us.
  AllocaInst *entryAlloca(const VarDecl &v) {
    IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tmp.CreateAlloca(ty(v.type), nullptr, v.name);
  }

  void function(const FunctionDecl &f) {
    fn = M.getFunction(f.name);
    B.SetInsertPoint(BasicBlock::Create(C, "entry", fn));
    unsigned i = 0;
    for (auto &arg : fn->args()) {
      const VarDecl &p = *f.params[i++];
      AllocaInst *slot = entryAlloca(p);
      B.CreateStore(&arg, slot);
      storage[&p] = slot;
    }
    block(*f.body);

    // Close any block left open: falling off the end, or dead code after a return.
    for (BasicBlock &bb : *fn) {
      if (bb.hasTerminator()) continue;
      B.SetInsertPoint(&bb);
      if (f.returnType == ::Type::Void) B.CreateRetVoid();
      else B.CreateRet(Constant::getNullValue(ty(f.returnType)));
    }
    if (verifyFunction(*fn, nullptr)) {
      std::string msg;
      raw_string_ostream os(msg);
      verifyFunction(*fn, &os);
      throw CompileError{"codegen", "internal error: invalid IR for '" + f.name + "': " + msg, f.loc};
    }
  }

  void ensureOpenBlock() {
    if (B.GetInsertBlock()->hasTerminator()) B.SetInsertPoint(BasicBlock::Create(C, "dead", fn));
  }

  void block(const BlockStmt &b) {
    for (auto &v : b.locals) storage[v.get()] = entryAlloca(*v);
    for (auto &s : b.stmts) stmt(*s);
  }

  void stmt(const Stmt &s) {
    ensureOpenBlock();
    switch (s.kind) {
    case Stmt::ExprS: {
      auto &e = static_cast<const ExprStmt &>(s);
      if (e.expr) expr(*e.expr);
      break;
    }
    case Stmt::BlockS: block(static_cast<const BlockStmt &>(s)); break;
    case Stmt::IfS: {
      auto &i = static_cast<const IfStmt &>(s);
      Value *c = expr(*i.cond);
      auto *thenBB = BasicBlock::Create(C, "if.then", fn);
      auto *elseBB = i.elseB ? BasicBlock::Create(C, "if.else", fn) : nullptr;
      auto *endBB = BasicBlock::Create(C, "if.end", fn);
      B.CreateCondBr(c, thenBB, elseBB ? elseBB : endBB);
      B.SetInsertPoint(thenBB);
      block(*i.thenB);
      if (!B.GetInsertBlock()->hasTerminator()) B.CreateBr(endBB);
      if (elseBB) {
        B.SetInsertPoint(elseBB);
        block(*i.elseB);
        if (!B.GetInsertBlock()->hasTerminator()) B.CreateBr(endBB);
      }
      B.SetInsertPoint(endBB);
      break;
    }
    case Stmt::WhileS: {
      auto &w = static_cast<const WhileStmt &>(s);
      auto *condBB = BasicBlock::Create(C, "while.cond", fn);
      auto *bodyBB = BasicBlock::Create(C, "while.body", fn);
      auto *endBB = BasicBlock::Create(C, "while.end", fn);
      B.CreateBr(condBB);
      B.SetInsertPoint(condBB);
      B.CreateCondBr(expr(*w.cond), bodyBB, endBB);
      B.SetInsertPoint(bodyBB);
      stmt(*w.body);
      if (!B.GetInsertBlock()->hasTerminator()) B.CreateBr(condBB);
      B.SetInsertPoint(endBB);
      break;
    }
    case Stmt::ReturnS: {
      auto &r = static_cast<const ReturnStmt &>(s);
      if (r.value) B.CreateRet(expr(*r.value));
      else B.CreateRetVoid();
      break;
    }
    }
  }

  Value *cast(Value *v, ::Type from, ::Type to) {
    if (from == to) return v;
    if (to == ::Type::Bool) {
      if (from == ::Type::Float) return B.CreateFCmpUNE(v, ConstantFP::get(B.getFloatTy(), 0.0), "tobool");
      return B.CreateICmpNE(v, ConstantInt::get(ty(from), 0), "tobool");
    }
    if (from == ::Type::Bool && to == ::Type::Int) return B.CreateZExt(v, B.getInt32Ty(), "conv");
    if (from == ::Type::Bool && to == ::Type::Float) return B.CreateUIToFP(v, B.getFloatTy(), "conv");
    if (from == ::Type::Int && to == ::Type::Float) return B.CreateSIToFP(v, B.getFloatTy(), "conv");
    throw CompileError{"codegen", "internal error: unsupported cast", {}};
  }

  // a && b  ==>  br a, rhs, end ; rhs: br end ; end: phi [false, lhs], [b, rhs]
  Value *shortCircuit(const BinaryExpr &b) {
    bool isAnd = b.op == AND;
    Value *lhs = expr(*b.lhs);
    BasicBlock *lhsEnd = B.GetInsertBlock();
    auto *rhsBB = BasicBlock::Create(C, isAnd ? "land.rhs" : "lor.rhs", fn);
    auto *endBB = BasicBlock::Create(C, isAnd ? "land.end" : "lor.end", fn);
    if (isAnd) B.CreateCondBr(lhs, rhsBB, endBB);
    else B.CreateCondBr(lhs, endBB, rhsBB);
    B.SetInsertPoint(rhsBB);
    Value *rhs = expr(*b.rhs);
    BasicBlock *rhsEnd = B.GetInsertBlock();
    B.CreateBr(endBB);
    B.SetInsertPoint(endBB);
    PHINode *phi = B.CreatePHI(B.getInt1Ty(), 2, isAnd ? "land" : "lor");
    phi->addIncoming(B.getInt1(!isAnd), lhsEnd);
    phi->addIncoming(rhs, rhsEnd);
    return phi;
  }

  Value *binary(const BinaryExpr &b) {
    if (b.op == AND || b.op == OR) return shortCircuit(b);
    Value *l = expr(*b.lhs);
    Value *r = expr(*b.rhs);
    bool fp = b.operandType == ::Type::Float;
    switch (b.op) {
    case PLUS: return fp ? B.CreateFAdd(l, r, "add") : B.CreateAdd(l, r, "add");
    case MINUS: return fp ? B.CreateFSub(l, r, "sub") : B.CreateSub(l, r, "sub");
    case ASTERIX: return fp ? B.CreateFMul(l, r, "mul") : B.CreateMul(l, r, "mul");
    case DIV: return fp ? B.CreateFDiv(l, r, "div") : B.CreateSDiv(l, r, "div");
    case MOD: return fp ? B.CreateFRem(l, r, "rem") : B.CreateSRem(l, r, "rem");
    case LT: return fp ? B.CreateFCmpOLT(l, r, "cmp") : B.CreateICmpSLT(l, r, "cmp");
    case LE: return fp ? B.CreateFCmpOLE(l, r, "cmp") : B.CreateICmpSLE(l, r, "cmp");
    case GT: return fp ? B.CreateFCmpOGT(l, r, "cmp") : B.CreateICmpSGT(l, r, "cmp");
    case GE: return fp ? B.CreateFCmpOGE(l, r, "cmp") : B.CreateICmpSGE(l, r, "cmp");
    case EQ: return fp ? B.CreateFCmpOEQ(l, r, "cmp") : B.CreateICmpEQ(l, r, "cmp");
    case NE: return fp ? B.CreateFCmpUNE(l, r, "cmp") : B.CreateICmpNE(l, r, "cmp");
    }
    throw CompileError{"codegen", "internal error: unknown binary operator", b.loc};
  }

  Value *expr(const Expr &e) {
    switch (e.kind) {
    case Expr::IntLit: return B.getInt32(static_cast<const IntLitExpr &>(e).value);
    case Expr::FloatLit: return ConstantFP::get(B.getFloatTy(), static_cast<const FloatLitExpr &>(e).value);
    case Expr::BoolLit: return B.getInt1(static_cast<const BoolLitExpr &>(e).value);
    case Expr::VarRef: {
      auto &v = static_cast<const VarRefExpr &>(e);
      return B.CreateLoad(ty(v.type), storage.at(v.decl), v.name);
    }
    case Expr::Assign: {
      auto &a = static_cast<const AssignExpr &>(e);
      Value *val = expr(*a.value);
      B.CreateStore(val, storage.at(a.decl));
      return val;
    }
    case Expr::Unary: {
      auto &u = static_cast<const UnaryExpr &>(e);
      Value *v = expr(*u.operand);
      if (u.op == NOT) return B.CreateNot(v, "not");
      return u.type == ::Type::Float ? B.CreateFNeg(v, "neg") : B.CreateNeg(v, "neg");
    }
    case Expr::Binary: return binary(static_cast<const BinaryExpr &>(e));
    case Expr::Call: {
      auto &c = static_cast<const CallExpr &>(e);
      std::vector<Value *> args;
      for (auto &a : c.args) args.push_back(expr(*a));
      Function *callee = M.getFunction(c.callee);
      return B.CreateCall(callee, args, c.type == ::Type::Void ? "" : "call");
    }
    case Expr::Cast: {
      auto &c = static_cast<const CastExpr &>(e);
      return cast(expr(*c.operand), c.operand->type, c.type);
    }
    }
    return nullptr;
  }
};

std::string print(const Module &m) {
  std::string s;
  raw_string_ostream os(s);
  m.print(os, nullptr);
  return s;
}

} // namespace

LLVMOutput generateLLVM(const Program &prog, const std::string &tripleStr, bool optimise, bool emitAsm) {
  static bool initialised = false;
  if (!initialised) {
    InitializeAllTargetInfos();
    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmPrinters();
    initialised = true;
  }

  LLVMContext ctx;
  auto mod = std::make_unique<Module>("mini-c", ctx);
  Triple triple(tripleStr.empty() ? sys::getProcessTriple() : tripleStr);

  std::string err;
  const Target *target = TargetRegistry::lookupTarget(triple, err);
  if (!target) throw CompileError{"codegen", "unknown target '" + triple.str() + "': " + err, {}};
  std::unique_ptr<TargetMachine> tm(
      target->createTargetMachine(triple, "generic", "", TargetOptions(), Reloc::PIC_, std::nullopt,
                                  optimise ? CodeGenOptLevel::Default : CodeGenOptLevel::None));
  // Plain coursework output (host, unoptimised) stays target-neutral so any clang
  // can pick its own default triple without an "overriding triple" warning.
  if (!tripleStr.empty() || optimise || emitAsm) {
    mod->setTargetTriple(triple);
    mod->setDataLayout(tm->createDataLayout());
  }

  IRGen(ctx, *mod).program(prog);
  if (verifyModule(*mod, nullptr)) {
    std::string msg;
    raw_string_ostream os(msg);
    verifyModule(*mod, &os);
    throw CompileError{"codegen", "internal error: module failed verification: " + msg, {}};
  }

  LLVMOutput out;
  out.ir = print(*mod);

  if (optimise) {
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    PassBuilder PB(tm.get());
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
    ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2);
    MPM.run(*mod, MAM);
    out.irOpt = print(*mod);
  }

  if (emitAsm) {
    SmallString<0> buf;
    raw_svector_ostream os(buf);
    legacy::PassManager pm;
    if (tm->addPassesToEmitFile(pm, os, nullptr, CodeGenFileType::AssemblyFile))
      throw CompileError{"codegen", "target cannot emit assembly", {}};
    pm.run(*mod);
    out.asmText = std::string(buf.str());
  }
  return out;
}
