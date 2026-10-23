#include "astdump.h"
#include <cstdio>

static std::string fmtFloat(float f) {
  char buf[32];
  snprintf(buf, sizeof buf, "%g", f);
  return buf;
}

static void dumpExpr(const Expr &e, TreeNode &parent, bool typed);
static void dumpStmt(const Stmt &s, TreeNode &parent, bool typed);

static std::string withType(std::string d, const Expr &e, bool typed) {
  if (!typed) return d;
  return d.empty() ? ": " + typeName(e.type) : d + " : " + typeName(e.type);
}

static void dumpExpr(const Expr &e, TreeNode &parent, bool typed) {
  switch (e.kind) {
  case Expr::IntLit:
    parent.add("IntLiteral", withType(std::to_string(static_cast<const IntLitExpr &>(e).value), e, typed), e.loc);
    break;
  case Expr::FloatLit:
    parent.add("FloatLiteral", withType(fmtFloat(static_cast<const FloatLitExpr &>(e).value), e, typed), e.loc);
    break;
  case Expr::BoolLit:
    parent.add("BoolLiteral", withType(static_cast<const BoolLitExpr &>(e).value ? "true" : "false", e, typed), e.loc);
    break;
  case Expr::VarRef: {
    auto &v = static_cast<const VarRefExpr &>(e);
    std::string d = v.name;
    if (v.decl) d += v.decl->isGlobal ? " (global)" : v.decl->isParam ? " (param)" : " (local)";
    parent.add("VarRef", withType(d, e, typed), e.loc);
    break;
  }
  case Expr::Assign: {
    auto &a = static_cast<const AssignExpr &>(e);
    TreeNode *n = parent.add("Assign", withType(a.name + " =", e, typed), e.loc);
    dumpExpr(*a.value, *n, typed);
    break;
  }
  case Expr::Unary: {
    auto &u = static_cast<const UnaryExpr &>(e);
    TreeNode *n = parent.add("UnaryExpr", withType("'" + opSpelling(u.op) + "'", e, typed), e.loc);
    dumpExpr(*u.operand, *n, typed);
    break;
  }
  case Expr::Binary: {
    auto &b = static_cast<const BinaryExpr &>(e);
    TreeNode *n = parent.add("BinaryExpr", withType("'" + opSpelling(b.op) + "'", e, typed), e.loc);
    dumpExpr(*b.lhs, *n, typed);
    dumpExpr(*b.rhs, *n, typed);
    break;
  }
  case Expr::Call: {
    auto &c = static_cast<const CallExpr &>(e);
    TreeNode *n = parent.add("Call", withType(c.callee + "()", e, typed), e.loc);
    for (auto &a : c.args) dumpExpr(*a, *n, typed);
    break;
  }
  case Expr::Cast: {
    auto &c = static_cast<const CastExpr &>(e);
    TreeNode *n = parent.add("ImplicitCast", typeName(c.operand->type) + " → " + typeName(c.type), e.loc);
    dumpExpr(*c.operand, *n, typed);
    break;
  }
  }
}

static void dumpBlock(const BlockStmt &b, TreeNode &parent, bool typed) {
  TreeNode *n = parent.add("Block", "", b.loc);
  for (auto &v : b.locals) n->add("VarDecl", typeName(v->type) + " " + v->name, v->loc);
  for (auto &s : b.stmts) dumpStmt(*s, *n, typed);
}

static void dumpStmt(const Stmt &s, TreeNode &parent, bool typed) {
  switch (s.kind) {
  case Stmt::ExprS: {
    auto &e = static_cast<const ExprStmt &>(s);
    if (e.expr) dumpExpr(*e.expr, parent, typed);
    else parent.add("EmptyStmt", "", s.loc);
    break;
  }
  case Stmt::BlockS: dumpBlock(static_cast<const BlockStmt &>(s), parent, typed); break;
  case Stmt::IfS: {
    auto &i = static_cast<const IfStmt &>(s);
    TreeNode *n = parent.add("IfStmt", i.elseB ? "has_else" : "", s.loc);
    dumpExpr(*i.cond, *n, typed);
    dumpBlock(*i.thenB, *n, typed);
    if (i.elseB) dumpBlock(*i.elseB, *n, typed);
    break;
  }
  case Stmt::WhileS: {
    auto &w = static_cast<const WhileStmt &>(s);
    TreeNode *n = parent.add("WhileStmt", "", s.loc);
    dumpExpr(*w.cond, *n, typed);
    dumpStmt(*w.body, *n, typed);
    break;
  }
  case Stmt::ReturnS: {
    auto &r = static_cast<const ReturnStmt &>(s);
    TreeNode *n = parent.add("ReturnStmt", "", s.loc);
    if (r.value) dumpExpr(*r.value, *n, typed);
    break;
  }
  }
}

std::unique_ptr<TreeNode> dumpAST(const Program &p, bool typed) {
  auto root = std::make_unique<TreeNode>();
  root->label = "Program";
  for (auto &item : p.items) {
    if (item.global) {
      root->add("GlobalVarDecl", typeName(item.global->type) + " " + item.global->name, item.global->loc);
      continue;
    }
    const FunctionDecl &f = *item.function;
    std::string sig = typeName(f.returnType) + " " + f.name + "(";
    for (size_t i = 0; i < f.params.size(); i++)
      sig += (i ? ", " : "") + typeName(f.params[i]->type) + " " + f.params[i]->name;
    sig += ")";
    TreeNode *n = root->add(f.isExtern() ? "ExternDecl" : "FunctionDecl", sig, f.loc);
    for (auto &p : f.params) n->add("ParamDecl", typeName(p->type) + " " + p->name, p->loc);
    if (f.body) dumpBlock(*f.body, *n, typed);
  }
  return root;
}
