#include "sema.h"
#include <algorithm>

std::string typeName(Type t) {
  switch (t) {
  case Type::Void: return "void";
  case Type::Bool: return "bool";
  case Type::Int: return "int";
  case Type::Float: return "float";
  }
  return "?";
}

std::string opSpelling(int op) { return tokenKindName(op); }

static std::string locStr(SrcLoc l) { return std::to_string(l.line) + ":" + std::to_string(l.col); }

void Sema::error(SrcLoc loc, const std::string &msg) { throw CompileError{"semantic", msg, loc}; }

void Sema::warn(SrcLoc loc, const std::string &msg) { warnings.push_back({"warning", "semantic", msg, loc}); }

std::string Sema::scopePath() const {
  std::string s;
  for (size_t i = 0; i < scopes.size(); i++) s += (i ? " › " : "") + scopes[i].name;
  return s;
}

void Sema::declare(VarDecl &v, const char *kind) {
  auto &vars = scopes.back().vars;
  auto it = vars.find(v.name);
  if (it != vars.end())
    error(v.loc, "redeclaration of '" + v.name + "' in the same scope (previous declaration at " +
                     locStr(it->second->loc) + ")");
  if (scopes.size() == 1 && functions.count(v.name))
    error(v.loc, "'" + v.name + "' is already declared as a function");
  v.id = nextId++;
  vars[v.name] = &v;
  symbols.push_back({scopePath(), v.name, kind, typeName(v.type), v.loc});
}

VarDecl *Sema::lookup(const std::string &name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto f = it->vars.find(name);
    if (f != it->vars.end()) return f->second;
  }
  return nullptr;
}

static bool sameSignature(const FunctionDecl &a, const FunctionDecl &b) {
  if (a.returnType != b.returnType || a.params.size() != b.params.size()) return false;
  for (size_t i = 0; i < a.params.size(); i++)
    if (a.params[i]->type != b.params[i]->type) return false;
  return true;
}

static std::string signature(const FunctionDecl &f) {
  std::string s = typeName(f.returnType) + " " + f.name + "(";
  for (size_t i = 0; i < f.params.size(); i++) s += (i ? ", " : "") + typeName(f.params[i]->type);
  return s + ")";
}

void Sema::run(Program &prog) {
  scopes.push_back({"global", {}});

  // Pass 1: collect every function signature so functions may call each other
  // regardless of definition order (mutual recursion without prototypes).
  for (auto &item : prog.items) {
    if (!item.function) continue;
    FunctionDecl &f = *item.function;
    auto it = functions.find(f.name);
    if (it != functions.end()) {
      FunctionDecl &prev = *it->second;
      if (!sameSignature(prev, f))
        error(f.loc, "conflicting declaration '" + signature(f) + "' (previously '" + signature(prev) +
                         "' at " + locStr(prev.loc) + ")");
      if (!prev.isExtern() && !f.isExtern())
        error(f.loc, "redefinition of function '" + f.name + "' (previous definition at " + locStr(prev.loc) + ")");
      if (!f.isExtern()) it->second = &f; // prefer the definition
      continue;
    }
    functions[f.name] = &f;
    symbols.push_back({"global", f.name, f.isExtern() ? "extern" : "function", signature(f), f.loc});
  }

  // Pass 2: globals are visible from their declaration onwards, as in C.
  for (auto &item : prog.items) {
    if (item.global) declare(*item.global, "global");
    else checkFunction(*item.function);
  }
}

void Sema::checkFunction(FunctionDecl &f) {
  currentFn = &f;
  scopes.push_back({f.name, {}});
  for (auto &p : f.params) declare(*p, "param");
  if (f.body) {
    // Parameters and the outermost block share a scope, so `int f(int x) { int x; }` is an error.
    checkBlock(*f.body, false);
    if (f.returnType != Type::Void && !alwaysReturns(*f.body))
      warn(f.loc, "non-void function '" + f.name + "' may reach the end without returning a value");
  }
  scopes.pop_back();
  currentFn = nullptr;
}

void Sema::checkBlock(BlockStmt &b, bool newScope) {
  if (newScope) scopes.push_back({"block@" + locStr(b.loc), {}});
  for (auto &v : b.locals) declare(*v, "local");
  bool returned = false;
  for (auto &s : b.stmts) {
    if (returned) {
      warn(s->loc, "unreachable code after return");
      returned = false; // only warn once per block
    }
    checkStmt(*s);
    if (s->kind == Stmt::ReturnS) returned = true;
  }
  if (newScope) scopes.pop_back();
}

bool Sema::alwaysReturns(const Stmt &s) const {
  switch (s.kind) {
  case Stmt::ReturnS: return true;
  case Stmt::BlockS: {
    auto &b = static_cast<const BlockStmt &>(s);
    return std::any_of(b.stmts.begin(), b.stmts.end(), [&](const StmtPtr &x) { return alwaysReturns(*x); });
  }
  case Stmt::IfS: {
    auto &i = static_cast<const IfStmt &>(s);
    return i.elseB && alwaysReturns(*i.thenB) && alwaysReturns(*i.elseB);
  }
  default: return false;
  }
}

void Sema::checkStmt(Stmt &s) {
  switch (s.kind) {
  case Stmt::ExprS: {
    auto &e = static_cast<ExprStmt &>(s);
    if (e.expr) checkExpr(e.expr);
    break;
  }
  case Stmt::BlockS: checkBlock(static_cast<BlockStmt &>(s), true); break;
  case Stmt::IfS: {
    auto &i = static_cast<IfStmt &>(s);
    if (i.cond->kind == Expr::Assign) warn(i.cond->loc, "assignment used as a condition; did you mean '=='?");
    checkExpr(i.cond);
    toCondition(i.cond, "if condition");
    checkBlock(*i.thenB, true);
    if (i.elseB) checkBlock(*i.elseB, true);
    break;
  }
  case Stmt::WhileS: {
    auto &w = static_cast<WhileStmt &>(s);
    if (w.cond->kind == Expr::Assign) warn(w.cond->loc, "assignment used as a condition; did you mean '=='?");
    checkExpr(w.cond);
    toCondition(w.cond, "while condition");
    checkStmt(*w.body);
    break;
  }
  case Stmt::ReturnS: {
    auto &r = static_cast<ReturnStmt &>(s);
    Type want = currentFn->returnType;
    if (!r.value) {
      if (want != Type::Void)
        error(r.loc, "non-void function '" + currentFn->name + "' must return a value of type " + typeName(want));
      break;
    }
    if (want == Type::Void) error(r.loc, "void function '" + currentFn->name + "' cannot return a value");
    checkExpr(r.value);
    coerce(r.value, want, "return value of '" + currentFn->name + "'");
    break;
  }
  }
}

// Implicit conversions are allowed only when they cannot lose information:
// bool → int → float. Anything else (float → int, int → bool, ...) is an error.
void Sema::coerce(ExprPtr &e, Type to, const std::string &context) {
  Type from = e->type;
  if (from == to) return;
  if (from == Type::Void) error(e->loc, "void value used in " + context);
  if (from < to) {
    e = std::make_unique<CastExpr>(std::move(e), to);
    return;
  }
  error(e->loc, "narrowing conversion from " + typeName(from) + " to " + typeName(to) + " in " + context +
                    " is not allowed");
}

// Conditions and logical operands may be any scalar; non-bools are tested
// against zero, exactly like C's truthiness. This is the one deliberate
// exception to the no-narrowing rule and it is explicit in the AST as a cast.
void Sema::toCondition(ExprPtr &e, const std::string &context) {
  if (e->type == Type::Void) error(e->loc, "void value used as " + context);
  if (e->type != Type::Bool) e = std::make_unique<CastExpr>(std::move(e), Type::Bool);
}

static bool isArithmetic(int op) { return op == PLUS || op == MINUS || op == ASTERIX || op == DIV || op == MOD; }
static bool isRelational(int op) { return op == LT || op == LE || op == GT || op == GE; }

void Sema::checkExpr(ExprPtr &e) {
  switch (e->kind) {
  case Expr::IntLit: e->type = Type::Int; break;
  case Expr::FloatLit: e->type = Type::Float; break;
  case Expr::BoolLit: e->type = Type::Bool; break;
  case Expr::VarRef: {
    auto &v = static_cast<VarRefExpr &>(*e);
    v.decl = lookup(v.name);
    if (!v.decl) {
      if (functions.count(v.name)) error(v.loc, "function '" + v.name + "' used as a variable (missing '()'?)");
      error(v.loc, "use of undeclared variable '" + v.name + "'");
    }
    v.type = v.decl->type;
    break;
  }
  case Expr::Assign: {
    auto &a = static_cast<AssignExpr &>(*e);
    a.decl = lookup(a.name);
    if (!a.decl) error(a.loc, "assignment to undeclared variable '" + a.name + "'");
    checkExpr(a.value);
    coerce(a.value, a.decl->type, "assignment to '" + a.name + "'");
    a.type = a.decl->type;
    break;
  }
  case Expr::Unary: {
    auto &u = static_cast<UnaryExpr &>(*e);
    checkExpr(u.operand);
    if (u.op == NOT) {
      toCondition(u.operand, "operand of '!'");
      u.type = Type::Bool;
    } else {
      if (u.operand->type == Type::Void) error(u.loc, "void value used as operand of unary '-'");
      if (u.operand->type == Type::Bool) coerce(u.operand, Type::Int, "operand of unary '-'");
      u.type = u.operand->type;
    }
    break;
  }
  case Expr::Binary: {
    auto &b = static_cast<BinaryExpr &>(*e);
    checkExpr(b.lhs);
    checkExpr(b.rhs);
    std::string ctx = "operand of '" + opSpelling(b.op) + "'";
    if (b.lhs->type == Type::Void || b.rhs->type == Type::Void) error(b.loc, "void value used as " + ctx);

    if (b.op == AND || b.op == OR) {
      toCondition(b.lhs, ctx);
      toCondition(b.rhs, ctx);
      b.operandType = b.type = Type::Bool;
      break;
    }
    // Usual arithmetic conversions: both sides go to the wider type, and bool
    // is promoted to int for arithmetic and ordering (true + true == 2).
    Type common = std::max(b.lhs->type, b.rhs->type);
    bool boolEquality = (b.op == EQ || b.op == NE) && common == Type::Bool;
    if (!boolEquality) common = std::max(common, Type::Int);
    coerce(b.lhs, common, ctx);
    coerce(b.rhs, common, ctx);
    b.operandType = common;
    b.type = isArithmetic(b.op) ? common : Type::Bool;
    if (isRelational(b.op) || b.op == EQ || b.op == NE) b.type = Type::Bool;
    if ((b.op == DIV || b.op == MOD) && common == Type::Int && b.rhs->kind == Expr::IntLit &&
        static_cast<IntLitExpr &>(*b.rhs).value == 0)
      warn(b.loc, "integer division by zero is undefined behaviour");
    break;
  }
  case Expr::Call: {
    auto &c = static_cast<CallExpr &>(*e);
    auto it = functions.find(c.callee);
    if (it == functions.end()) {
      if (lookup(c.callee)) error(c.loc, "'" + c.callee + "' is a variable, not a function");
      error(c.loc, "call to undeclared function '" + c.callee + "'");
    }
    c.decl = it->second;
    auto &params = c.decl->params;
    if (c.args.size() != params.size())
      error(c.loc, "function '" + signature(*c.decl) + "' expects " + std::to_string(params.size()) +
                       " argument" + (params.size() == 1 ? "" : "s") + " but " + std::to_string(c.args.size()) +
                       (c.args.size() == 1 ? " was" : " were") + " given");
    for (size_t i = 0; i < c.args.size(); i++) {
      checkExpr(c.args[i]);
      coerce(c.args[i], params[i]->type,
             "argument " + std::to_string(i + 1) + " ('" + params[i]->name + "') of '" + c.callee + "'");
    }
    c.type = c.decl->returnType;
    break;
  }
  case Expr::Cast: checkExpr(static_cast<CastExpr &>(*e).operand); break;
  }
}
