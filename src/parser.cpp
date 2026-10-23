#include "parser.h"
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <exception>

// RAII helper: each parse function opens a parse-tree node named after its
// non-terminal. A node that consumed nothing is an epsilon expansion.
struct Parser::Rule {
  Parser &p;
  TreeNode *saved;
  Rule(Parser &parser, const char *name) : p(parser), saved(parser.cur) {
    if (p.cur) p.cur = p.cur->add(name, "", p.tok().loc);
  }
  ~Rule() {
    if (!p.cur) return;
    if (p.cur->children.empty() && !std::uncaught_exceptions()) p.cur->add("ε", "", {}, true);
    p.cur = saved;
  }
};

static std::string describe(const Token &t) {
  if (t.type == EOF_TOK) return "end of file";
  if (t.type == IDENT) return "identifier '" + t.lexeme + "'";
  if (t.type == INT_LIT || t.type == FLOAT_LIT || t.type == BOOL_LIT) return "literal '" + t.lexeme + "'";
  return "'" + t.lexeme + "'";
}

void Parser::fail(const std::string &msg) const { throw CompileError{"parser", msg, tok().loc}; }

void Parser::failExpected(const std::string &expected) const {
  fail("expected " + expected + ", found " + describe(tok()));
}

Token Parser::eat(int type, const std::string &what) {
  if (!at(type)) failExpected(what);
  Token t = tok();
  if (cur) cur->add(tokenKindName(type), t.lexeme, t.loc, true);
  if (idx + 1 < toks.size()) idx++;
  return t;
}

void Parser::checkFollow(const std::set<int> &follow, const std::string &expected) const {
  if (!follow.count(tok().type)) failExpected(expected);
}

bool Parser::atStmtStart() const {
  switch (tok().type) {
  case IDENT: case INT_LIT: case FLOAT_LIT: case BOOL_LIT: case LPAR: case MINUS:
  case NOT: case SC: case LBRA: case IF: case WHILE: case RETURN:
    return true;
  default:
    return false;
  }
}

static const std::set<int> TYPE_TOKENS = {INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK};

//===--- Declarations ------------------------------------------------------===//

// program ::= extern_list decl_list | decl_list
std::unique_ptr<Program> Parser::parseProgram() {
  auto prog = std::make_unique<Program>();
  TreeNode holder;
  if (record) cur = &holder;
  auto keepTree = [&] {
    if (record && !holder.children.empty()) parseTree = std::move(holder.children[0]);
    cur = nullptr;
  };
  try {
    Rule r(*this, "program");
    if (at(EXTERN)) externList(*prog);
    declList(*prog);
    if (!at(EOF_TOK)) failExpected("a declaration or end of file");
  } catch (...) {
    keepTree(); // a partial tree is still useful for showing where parsing stopped
    throw;
  }
  keepTree();
  return prog;
}

// extern_list ::= extern extern_list'
void Parser::externList(Program &p) {
  Rule r(*this, "extern_list");
  externDecl(p);
  externListPrime(p);
}

// extern_list' ::= extern extern_list' | ε          FOLLOW = FIRST(decl_list)
void Parser::externListPrime(Program &p) {
  Rule r(*this, "extern_list'");
  if (at(EXTERN)) {
    externDecl(p);
    externListPrime(p);
  } else {
    checkFollow(TYPE_TOKENS, "'extern' or a declaration (after externs, at least one definition is required)");
  }
}

// extern ::= "extern" type_spec IDENT "(" params ")" ";"
void Parser::externDecl(Program &p) {
  Rule r(*this, "extern");
  eat(EXTERN, "'extern'");
  auto f = std::make_unique<FunctionDecl>();
  f->returnType = typeSpec();
  Token name = eat(IDENT, "function name");
  f->name = name.lexeme;
  f->loc = name.loc;
  eat(LPAR, "'('");
  params(*f);
  eat(RPAR, "')'");
  eat(SC, "';' after extern declaration");
  p.items.push_back({nullptr, std::move(f)});
}

// decl_list ::= decl decl_list'
void Parser::declList(Program &p) {
  Rule r(*this, "decl_list");
  decl(p);
  declListPrime(p);
}

// decl_list' ::= decl decl_list' | ε               FOLLOW = { EOF }
void Parser::declListPrime(Program &p) {
  Rule r(*this, "decl_list'");
  if (TYPE_TOKENS.count(tok().type)) {
    decl(p);
    declListPrime(p);
  } else {
    checkFollow({EOF_TOK}, "a declaration or end of file");
  }
}

// The original grammar has  decl ::= var_decl | fun_decl  where both start with
// "type IDENT", an LL(3) decision. Left-factoring removes it:
//   decl      ::= type_spec IDENT decl_tail
//   decl_tail ::= ";" | "(" params ")" block
void Parser::decl(Program &p) {
  Rule r(*this, "decl");
  if (!TYPE_TOKENS.count(tok().type)) failExpected("a type ('int', 'float', 'bool' or 'void')");
  Type t = typeSpec();
  Token name = eat(IDENT, "identifier");
  declTail(p, t, name);
}

void Parser::declTail(Program &p, Type t, const Token &name) {
  Rule r(*this, "decl_tail");
  if (at(SC)) {
    eat(SC, "';'");
    if (t == Type::Void) throw CompileError{"parser", "variable '" + name.lexeme + "' declared void", name.loc};
    auto v = std::make_unique<VarDecl>();
    v->type = t;
    v->name = name.lexeme;
    v->loc = name.loc;
    v->isGlobal = true;
    p.items.push_back({std::move(v), nullptr});
  } else if (at(LPAR)) {
    auto f = std::make_unique<FunctionDecl>();
    f->returnType = t;
    f->name = name.lexeme;
    f->loc = name.loc;
    eat(LPAR, "'('");
    params(*f);
    eat(RPAR, "')'");
    f->body = block();
    p.items.push_back({nullptr, std::move(f)});
  } else {
    failExpected("';' or '(' after '" + name.lexeme + "'");
  }
}

// type_spec ::= "void" | var_type
Type Parser::typeSpec() {
  Rule r(*this, "type_spec");
  if (at(VOID_TOK)) {
    eat(VOID_TOK, "'void'");
    return Type::Void;
  }
  return varType();
}

// var_type ::= "int" | "float" | "bool"
Type Parser::varType() {
  Rule r(*this, "var_type");
  if (at(INT_TOK)) { eat(INT_TOK, "'int'"); return Type::Int; }
  if (at(FLOAT_TOK)) { eat(FLOAT_TOK, "'float'"); return Type::Float; }
  if (at(BOOL_TOK)) { eat(BOOL_TOK, "'bool'"); return Type::Bool; }
  failExpected("a type ('int', 'float' or 'bool')");
}

// params ::= param_list | "void" | ε
void Parser::params(FunctionDecl &f) {
  Rule r(*this, "params");
  if (at(VOID_TOK)) eat(VOID_TOK, "'void'");
  else if (atVarType()) paramList(f);
  else checkFollow({RPAR}, "a parameter type or ')'");
}

// param_list ::= param param_list'
void Parser::paramList(FunctionDecl &f) {
  Rule r(*this, "param_list");
  param(f);
  paramListPrime(f);
}

// param_list' ::= "," param param_list' | ε
void Parser::paramListPrime(FunctionDecl &f) {
  Rule r(*this, "param_list'");
  if (at(COMMA)) {
    eat(COMMA, "','");
    param(f);
    paramListPrime(f);
  } else {
    checkFollow({RPAR}, "',' or ')' in parameter list");
  }
}

// param ::= var_type IDENT
void Parser::param(FunctionDecl &f) {
  Rule r(*this, "param");
  auto v = std::make_unique<VarDecl>();
  if (at(VOID_TOK)) fail("parameter cannot have type 'void'");
  v->type = varType();
  Token name = eat(IDENT, "parameter name");
  v->name = name.lexeme;
  v->loc = name.loc;
  v->isParam = true;
  f.params.push_back(std::move(v));
}

//===--- Blocks and statements ---------------------------------------------===//

// block ::= "{" local_decls stmt_list "}"
std::unique_ptr<BlockStmt> Parser::block() {
  Rule r(*this, "block");
  auto b = std::make_unique<BlockStmt>(tok().loc);
  eat(LBRA, "'{'");
  localDecls(*b);
  stmtList(*b);
  eat(RBRA, "'}'");
  return b;
}

// local_decls ::= local_decl local_decls | ε        (right-recursive form of the left-recursive original)
void Parser::localDecls(BlockStmt &b) {
  Rule r(*this, "local_decls");
  if (atVarType()) {
    localDecl(b);
    localDecls(b);
  } else if (at(VOID_TOK)) {
    fail("local variable cannot have type 'void'");
  } else if (!atStmtStart() && !at(RBRA)) {
    failExpected("a declaration, statement or '}'");
  }
}

// local_decl ::= var_type IDENT ";"
void Parser::localDecl(BlockStmt &b) {
  Rule r(*this, "local_decl");
  auto v = std::make_unique<VarDecl>();
  v->type = varType();
  Token name = eat(IDENT, "variable name");
  v->name = name.lexeme;
  v->loc = name.loc;
  eat(SC, "';' after variable declaration");
  b.locals.push_back(std::move(v));
}

// stmt_list ::= stmt stmt_list | ε                  FOLLOW = { "}" }
void Parser::stmtList(BlockStmt &b) {
  Rule r(*this, "stmt_list");
  if (atStmtStart()) {
    b.stmts.push_back(stmt());
    stmtList(b);
  } else if (TYPE_TOKENS.count(tok().type)) {
    fail("declarations must appear at the start of a block, before any statements");
  } else {
    checkFollow({RBRA}, "a statement or '}'");
  }
}

// stmt ::= expr_stmt | block | if_stmt | while_stmt | return_stmt
StmtPtr Parser::stmt() {
  Rule r(*this, "stmt");
  switch (tok().type) {
  case LBRA: return block();
  case IF: return ifStmt();
  case WHILE: return whileStmt();
  case RETURN: return returnStmt();
  default: return exprStmt();
  }
}

// expr_stmt ::= expr ";" | ";"
StmtPtr Parser::exprStmt() {
  Rule r(*this, "expr_stmt");
  SrcLoc loc = tok().loc;
  if (at(SC)) {
    eat(SC, "';'");
    return std::make_unique<ExprStmt>(loc, nullptr);
  }
  ExprPtr e = expr();
  eat(SC, "';' after expression");
  return std::make_unique<ExprStmt>(loc, std::move(e));
}

// while_stmt ::= "while" "(" expr ")" stmt
StmtPtr Parser::whileStmt() {
  Rule r(*this, "while_stmt");
  SrcLoc loc = eat(WHILE, "'while'").loc;
  eat(LPAR, "'(' after 'while'");
  ExprPtr c = expr();
  eat(RPAR, "')' after loop condition");
  StmtPtr body = stmt();
  return std::make_unique<WhileStmt>(loc, std::move(c), std::move(body));
}

// if_stmt ::= "if" "(" expr ")" block else_stmt
// Both branches must be blocks, so the dangling-else ambiguity cannot arise.
StmtPtr Parser::ifStmt() {
  Rule r(*this, "if_stmt");
  SrcLoc loc = eat(IF, "'if'").loc;
  eat(LPAR, "'(' after 'if'");
  ExprPtr c = expr();
  eat(RPAR, "')' after if condition");
  if (!at(LBRA)) failExpected("'{' (Mini-C requires braces around if bodies)");
  auto thenB = block();
  auto elseB = elseStmt();
  return std::make_unique<IfStmt>(loc, std::move(c), std::move(thenB), std::move(elseB));
}

// else_stmt ::= "else" block | ε
std::unique_ptr<BlockStmt> Parser::elseStmt() {
  Rule r(*this, "else_stmt");
  if (!at(ELSE)) {
    if (!atStmtStart() && !at(RBRA)) failExpected("'else', a statement or '}'");
    return nullptr;
  }
  eat(ELSE, "'else'");
  if (!at(LBRA)) failExpected("'{' (Mini-C requires braces around else bodies)");
  return block();
}

// The original  "return" ";" | "return" expr ";"  share a prefix; left-factored:
//   return_stmt  ::= "return" return_value ";"
//   return_value ::= expr | ε                       FOLLOW = { ";" }
StmtPtr Parser::returnStmt() {
  Rule r(*this, "return_stmt");
  SrcLoc loc = eat(RETURN, "'return'").loc;
  ExprPtr v;
  {
    Rule rv(*this, "return_value");
    if (!at(SC)) v = expr();
  }
  eat(SC, "';' after return");
  return std::make_unique<ReturnStmt>(loc, std::move(v));
}

//===--- Expressions -------------------------------------------------------===//

// expr ::= IDENT "=" expr | or_expr
// FIRST(IDENT "=" expr) ∩ FIRST(or_expr) = { IDENT }, so this is the one genuine
// LL(1) conflict in the grammar. It cannot be left-factored away without
// destroying the precedence chain, so we peek one extra token: LL(2) here only.
ExprPtr Parser::expr() {
  Rule r(*this, "expr");
  if (at(IDENT) && tok(1).type == ASSIGN) {
    Token name = eat(IDENT, "identifier");
    eat(ASSIGN, "'='");
    ExprPtr value = expr(); // right recursion = right associativity: a = b = c
    return std::make_unique<AssignExpr>(name.loc, name.lexeme, std::move(value));
  }
  return binaryLevel(0);
}

// Precedence climbing via one non-terminal per level, lowest first. After
// left-recursion elimination each level has the shape
//   level  ::= next level'
//   level' ::= op next level' | ε
// The primed function receives the tree built so far as an inherited attribute
// (`lhs`), so  a - b - c  still builds ((a - b) - c): left associative.
struct Level {
  const char *name;
  const char *primeName;
  std::set<int> ops;
};
static const Level LEVELS[] = {
    {"or_expr", "or_expr'", {OR}},
    {"and_expr", "and_expr'", {AND}},
    {"equality_expr", "equality_expr'", {EQ, NE}},
    {"relational_expr", "relational_expr'", {LT, LE, GT, GE}},
    {"additive_expr", "additive_expr'", {PLUS, MINUS}},
    {"multiplicative_expr", "multiplicative_expr'", {ASTERIX, DIV, MOD}},
};
static const int NUM_LEVELS = sizeof(LEVELS) / sizeof(LEVELS[0]);

ExprPtr Parser::binaryLevel(int level) {
  if (level == NUM_LEVELS) return unaryExpr();
  Rule r(*this, LEVELS[level].name);
  ExprPtr lhs = binaryLevel(level + 1);
  return binaryLevelPrime(level, std::move(lhs));
}

ExprPtr Parser::binaryLevelPrime(int level, ExprPtr lhs) {
  Rule r(*this, LEVELS[level].primeName);
  if (LEVELS[level].ops.count(tok().type)) {
    Token op = eat(tok().type, "operator");
    ExprPtr rhs = binaryLevel(level + 1);
    auto node = std::make_unique<BinaryExpr>(op.loc, op.type, std::move(lhs), std::move(rhs));
    return binaryLevelPrime(level, std::move(node));
  }
  // ε: the lookahead must be in FOLLOW(level'), i.e. an operator of a lower
  // precedence level or something that can end an expression. The innermost
  // level's FOLLOW set is a superset of all the others, so checking there is enough.
  if (level == NUM_LEVELS - 1) {
    std::set<int> follow = {SC, RPAR, COMMA};
    for (int l = 0; l < level; l++) follow.insert(LEVELS[l].ops.begin(), LEVELS[l].ops.end());
    if (!follow.count(tok().type)) {
      // A missing ';' is by far the likeliest cause when the expression is
      // followed by '}' or by something on a new line: point just past the
      // previous token, like clang does.
      const Token &prev = toks[idx - 1];
      if (at(RBRA) || tok().loc.line > prev.loc.line)
        throw CompileError{"parser", "expected ';' after expression",
                           {prev.loc.line, prev.loc.col + (int)prev.lexeme.size()}};
      failExpected("an operator, ';', ')' or ','");
    }
  }
  return lhs;
}

// unary_expr ::= "-" unary_expr | "!" unary_expr | primary
ExprPtr Parser::unaryExpr() {
  Rule r(*this, "unary_expr");
  if (at(MINUS) || at(NOT)) {
    Token op = eat(tok().type, "unary operator");
    return std::make_unique<UnaryExpr>(op.loc, op.type, unaryExpr());
  }
  return primary();
}

// primary    ::= IDENT ident_tail | INT_LIT | FLOAT_LIT | BOOL_LIT | "(" expr ")"
// ident_tail ::= "(" args ")" | ε
// The original  IDENT | IDENT "(" args ")"  is left-factored into ident_tail.
ExprPtr Parser::primary() {
  Rule r(*this, "primary");
  if (at(IDENT)) {
    Token name = eat(IDENT, "identifier");
    Rule tail(*this, "ident_tail");
    if (at(LPAR)) {
      eat(LPAR, "'('");
      auto a = args();
      eat(RPAR, "')' after arguments");
      return std::make_unique<CallExpr>(name.loc, name.lexeme, std::move(a));
    }
    return std::make_unique<VarRefExpr>(name.loc, name.lexeme);
  }
  if (at(INT_LIT)) {
    Token t = eat(INT_LIT, "integer literal");
    errno = 0;
    long long v = strtoll(t.lexeme.c_str(), nullptr, 10);
    if (errno == ERANGE || v > INT_MAX)
      throw CompileError{"parser", "integer literal '" + t.lexeme + "' does not fit in 32-bit int", t.loc};
    return std::make_unique<IntLitExpr>(t.loc, (int)v);
  }
  if (at(FLOAT_LIT)) {
    Token t = eat(FLOAT_LIT, "float literal");
    return std::make_unique<FloatLitExpr>(t.loc, strtof(t.lexeme.c_str(), nullptr));
  }
  if (at(BOOL_LIT)) {
    Token t = eat(BOOL_LIT, "bool literal");
    return std::make_unique<BoolLitExpr>(t.loc, t.lexeme == "true");
  }
  if (at(LPAR)) {
    eat(LPAR, "'('");
    ExprPtr e = expr();
    eat(RPAR, "')'");
    return e;
  }
  failExpected("an expression");
}

// args ::= arg_list | ε
std::vector<ExprPtr> Parser::args() {
  Rule r(*this, "args");
  std::vector<ExprPtr> out;
  if (!at(RPAR)) argList(out);
  return out;
}

// arg_list ::= expr arg_list'
void Parser::argList(std::vector<ExprPtr> &out) {
  Rule r(*this, "arg_list");
  out.push_back(expr());
  argListPrime(out);
}

// arg_list' ::= "," expr arg_list' | ε
void Parser::argListPrime(std::vector<ExprPtr> &out) {
  Rule r(*this, "arg_list'");
  if (at(COMMA)) {
    eat(COMMA, "','");
    out.push_back(expr());
    argListPrime(out);
  } else {
    checkFollow({RPAR}, "',' or ')' in argument list");
  }
}
