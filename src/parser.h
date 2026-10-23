#pragma once
#include "ast.h"
#include "tree.h"
#include <set>

// Predictive recursive-descent parser for the LL(1)-transformed Mini-C grammar
// in grammar/minic-ll1.bnf. Exactly one parse function per non-terminal; the
// only places that need a second token of lookahead are documented in expr().
class Parser {
public:
  Parser(std::vector<Token> tokens, bool recordParseTree)
      : toks(std::move(tokens)), record(recordParseTree) {}

  std::unique_ptr<Program> parseProgram();
  // Concrete parse tree (one node per non-terminal expansion); null unless recorded.
  std::unique_ptr<TreeNode> parseTree;

private:
  std::vector<Token> toks;
  size_t idx = 0;
  bool record;
  TreeNode *cur = nullptr;
  struct Rule;

  const Token &tok(size_t ahead = 0) const {
    size_t i = idx + ahead;
    return toks[i < toks.size() ? i : toks.size() - 1];
  }
  bool at(int type) const { return tok().type == type; }
  bool atVarType() const { return at(INT_TOK) || at(FLOAT_TOK) || at(BOOL_TOK); }
  bool atStmtStart() const;
  Token eat(int type, const std::string &what);
  [[noreturn]] void fail(const std::string &msg) const;
  [[noreturn]] void failExpected(const std::string &expected) const;
  void checkFollow(const std::set<int> &follow, const std::string &expected) const;

  void externList(Program &p);
  void externListPrime(Program &p);
  void externDecl(Program &p);
  void declList(Program &p);
  void declListPrime(Program &p);
  void decl(Program &p);
  void declTail(Program &p, Type t, const Token &name);
  Type typeSpec();
  Type varType();
  void params(FunctionDecl &f);
  void paramList(FunctionDecl &f);
  void paramListPrime(FunctionDecl &f);
  void param(FunctionDecl &f);
  std::unique_ptr<BlockStmt> block();
  void localDecls(BlockStmt &b);
  void localDecl(BlockStmt &b);
  void stmtList(BlockStmt &b);
  StmtPtr stmt();
  StmtPtr exprStmt();
  StmtPtr whileStmt();
  StmtPtr ifStmt();
  std::unique_ptr<BlockStmt> elseStmt();
  StmtPtr returnStmt();
  ExprPtr expr();
  ExprPtr binaryLevel(int level);
  ExprPtr binaryLevelPrime(int level, ExprPtr lhs);
  ExprPtr unaryExpr();
  ExprPtr primary();
  std::vector<ExprPtr> args();
  void argList(std::vector<ExprPtr> &out);
  void argListPrime(std::vector<ExprPtr> &out);
};
