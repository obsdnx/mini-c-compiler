#pragma once
#include "lexer.h"
#include <memory>
#include <string>
#include <vector>

// Mini-C types, ordered so that a "wider" type compares greater (bool < int < float).
enum class Type { Void, Bool, Int, Float };
std::string typeName(Type t);

struct VarDecl;
struct FunctionDecl;

//===--- Expressions -------------------------------------------------------===//

struct Expr {
  enum Kind { IntLit, FloatLit, BoolLit, VarRef, Assign, Unary, Binary, Call, Cast };
  const Kind kind;
  SrcLoc loc;
  Type type = Type::Void; // filled in by semantic analysis
  Expr(Kind k, SrcLoc l) : kind(k), loc(l) {}
  virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLitExpr : Expr {
  int value;
  IntLitExpr(SrcLoc l, int v) : Expr(IntLit, l), value(v) {}
};
struct FloatLitExpr : Expr {
  float value;
  FloatLitExpr(SrcLoc l, float v) : Expr(FloatLit, l), value(v) {}
};
struct BoolLitExpr : Expr {
  bool value;
  BoolLitExpr(SrcLoc l, bool v) : Expr(BoolLit, l), value(v) {}
};
struct VarRefExpr : Expr {
  std::string name;
  VarDecl *decl = nullptr; // resolved by sema
  VarRefExpr(SrcLoc l, std::string n) : Expr(VarRef, l), name(std::move(n)) {}
};
struct AssignExpr : Expr {
  std::string name;
  ExprPtr value;
  VarDecl *decl = nullptr;
  AssignExpr(SrcLoc l, std::string n, ExprPtr v)
      : Expr(Assign, l), name(std::move(n)), value(std::move(v)) {}
};
struct UnaryExpr : Expr {
  int op; // MINUS or NOT
  ExprPtr operand;
  UnaryExpr(SrcLoc l, int o, ExprPtr e) : Expr(Unary, l), op(o), operand(std::move(e)) {}
};
struct BinaryExpr : Expr {
  int op;
  ExprPtr lhs, rhs;
  Type operandType = Type::Void; // common type both sides were converted to
  BinaryExpr(SrcLoc l, int o, ExprPtr a, ExprPtr b)
      : Expr(Binary, l), op(o), lhs(std::move(a)), rhs(std::move(b)) {}
};
struct CallExpr : Expr {
  std::string callee;
  std::vector<ExprPtr> args;
  FunctionDecl *decl = nullptr;
  CallExpr(SrcLoc l, std::string c, std::vector<ExprPtr> a)
      : Expr(Call, l), callee(std::move(c)), args(std::move(a)) {}
};
// Implicit conversion inserted by sema (never written by the programmer).
struct CastExpr : Expr {
  ExprPtr operand;
  CastExpr(ExprPtr e, Type to) : Expr(Cast, e->loc), operand(std::move(e)) { type = to; }
};

//===--- Statements --------------------------------------------------------===//

struct VarDecl {
  Type type;
  std::string name;
  SrcLoc loc;
  bool isGlobal = false;
  bool isParam = false;
  int id = 0; // unique per program, lets backends key storage without name clashes
};
using VarDeclPtr = std::unique_ptr<VarDecl>;

struct Stmt {
  enum Kind { ExprS, BlockS, IfS, WhileS, ReturnS };
  const Kind kind;
  SrcLoc loc;
  Stmt(Kind k, SrcLoc l) : kind(k), loc(l) {}
  virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct ExprStmt : Stmt {
  ExprPtr expr; // null for the empty statement ";"
  ExprStmt(SrcLoc l, ExprPtr e) : Stmt(ExprS, l), expr(std::move(e)) {}
};
struct BlockStmt : Stmt {
  std::vector<VarDeclPtr> locals;
  std::vector<StmtPtr> stmts;
  explicit BlockStmt(SrcLoc l) : Stmt(BlockS, l) {}
};
struct IfStmt : Stmt {
  ExprPtr cond;
  std::unique_ptr<BlockStmt> thenB, elseB;
  IfStmt(SrcLoc l, ExprPtr c, std::unique_ptr<BlockStmt> t, std::unique_ptr<BlockStmt> e)
      : Stmt(IfS, l), cond(std::move(c)), thenB(std::move(t)), elseB(std::move(e)) {}
};
struct WhileStmt : Stmt {
  ExprPtr cond;
  StmtPtr body;
  WhileStmt(SrcLoc l, ExprPtr c, StmtPtr b) : Stmt(WhileS, l), cond(std::move(c)), body(std::move(b)) {}
};
struct ReturnStmt : Stmt {
  ExprPtr value;
  ReturnStmt(SrcLoc l, ExprPtr v) : Stmt(ReturnS, l), value(std::move(v)) {}
};

//===--- Top level ---------------------------------------------------------===//

struct FunctionDecl {
  Type returnType;
  std::string name;
  SrcLoc loc;
  std::vector<VarDeclPtr> params;
  std::unique_ptr<BlockStmt> body; // null for extern declarations
  bool isExtern() const { return body == nullptr; }
};

struct Program {
  // Top-level items in source order (exactly one of the two is set per item).
  struct Item {
    VarDeclPtr global;
    std::unique_ptr<FunctionDecl> function;
  };
  std::vector<Item> items;
};

std::string opSpelling(int op);
