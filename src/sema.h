#pragma once
#include "ast.h"
#include <unordered_map>

struct Diagnostic {
  std::string severity; // "error" | "warning"
  std::string stage;
  std::string message;
  SrcLoc loc;
};

// One row of the symbol-table dump shown by --emit=symbols and the visualiser.
struct SymbolRow {
  std::string scope; // e.g. "global", "fact", "fact › block@4:3"
  std::string name;
  std::string kind; // "function" | "extern" | "global" | "param" | "local"
  std::string type;
  SrcLoc loc;
};

// Resolves every name to its declaration, computes the type of every
// expression, inserts explicit CastExpr nodes for the implicit widening
// conversions, and rejects anything that would need a narrowing conversion.
class Sema {
public:
  void run(Program &prog); // throws CompileError on the first error
  std::vector<SymbolRow> symbols;
  std::vector<Diagnostic> warnings;

private:
  struct Scope {
    std::string name;
    std::unordered_map<std::string, VarDecl *> vars;
  };
  std::vector<Scope> scopes;
  std::unordered_map<std::string, FunctionDecl *> functions;
  FunctionDecl *currentFn = nullptr;
  int nextId = 0;

  [[noreturn]] void error(SrcLoc loc, const std::string &msg);
  void warn(SrcLoc loc, const std::string &msg);
  void declare(VarDecl &v, const char *kind);
  VarDecl *lookup(const std::string &name);
  std::string scopePath() const;

  void checkFunction(FunctionDecl &f);
  void checkBlock(BlockStmt &b, bool newScope);
  void checkStmt(Stmt &s);
  void checkExpr(ExprPtr &e);
  void coerce(ExprPtr &e, Type to, const std::string &context);
  void toCondition(ExprPtr &e, const std::string &context);
  bool alwaysReturns(const Stmt &s) const;
};
