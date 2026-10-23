#include "lexer.h"
#include <cctype>
#include <cstdio>
#include <unordered_map>

std::string tokenKindName(int t) {
  switch (t) {
  case IDENT: return "IDENT";
  case INT_TOK: return "int";
  case VOID_TOK: return "void";
  case FLOAT_TOK: return "float";
  case BOOL_TOK: return "bool";
  case EXTERN: return "extern";
  case IF: return "if";
  case ELSE: return "else";
  case WHILE: return "while";
  case RETURN: return "return";
  case INT_LIT: return "INT_LIT";
  case FLOAT_LIT: return "FLOAT_LIT";
  case BOOL_LIT: return "BOOL_LIT";
  case AND: return "&&";
  case OR: return "||";
  case EQ: return "==";
  case NE: return "!=";
  case LE: return "<=";
  case GE: return ">=";
  case EOF_TOK: return "EOF";
  case INVALID: return "INVALID";
  default:
    if (t > 0 && t < 128) return std::string(1, (char)t);
    return "?";
  }
}

int Lexer::advance() {
  int c = peek();
  if (c == EOF) return c;
  pos++;
  if (c == '\n') {
    line++;
    col = 1;
  } else {
    col++;
  }
  return c;
}

Token Lexer::next() {
  // Skip whitespace and comments (// line and /* block */).
  for (;;) {
    int c = peek();
    if (c != EOF && isspace(c)) {
      advance();
    } else if (c == '/' && peek(1) == '/') {
      while (peek() != EOF && peek() != '\n') advance();
    } else if (c == '/' && peek(1) == '*') {
      SrcLoc start{line, col};
      advance();
      advance();
      while (!(peek() == '*' && peek(1) == '/')) {
        if (peek() == EOF) throw CompileError{"lexer", "unterminated block comment", start};
        advance();
      }
      advance();
      advance();
    } else {
      break;
    }
  }

  SrcLoc start{line, col};
  size_t startPos = pos;
  int c = peek();

  if (c == EOF) return Token{EOF_TOK, "", start};

  // Identifiers and keywords: [a-zA-Z_][a-zA-Z_0-9]*
  if (isalpha(c) || c == '_') {
    while (isalnum(peek()) || peek() == '_') advance();
    static const std::unordered_map<std::string, int> keywords = {
        {"int", INT_TOK},   {"float", FLOAT_TOK}, {"bool", BOOL_TOK},
        {"void", VOID_TOK}, {"extern", EXTERN},   {"if", IF},
        {"else", ELSE},     {"while", WHILE},     {"return", RETURN},
        {"true", BOOL_LIT}, {"false", BOOL_LIT}};
    Token t = make(IDENT, start, startPos);
    auto it = keywords.find(t.lexeme);
    if (it != keywords.end()) t.type = it->second;
    return t;
  }

  // Numbers: [0-9]+ | [0-9]+"."[0-9]* | "."[0-9]+
  if (isdigit(c) || (c == '.' && isdigit(peek(1)))) {
    bool isFloat = false;
    while (isdigit(peek())) advance();
    if (peek() == '.') {
      isFloat = true;
      advance();
      while (isdigit(peek())) advance();
    }
    return make(isFloat ? FLOAT_LIT : INT_LIT, start, startPos);
  }

  advance();
  // Maximal munch: always prefer the longest operator.
  auto two = [&](int second, int longType, int shortType) {
    if (peek() == second) {
      advance();
      return make(longType, start, startPos);
    }
    return make(shortType, start, startPos);
  };

  switch (c) {
  case '=': return two('=', EQ, ASSIGN);
  case '!': return two('=', NE, NOT);
  case '<': return two('=', LE, LT);
  case '>': return two('=', GE, GT);
  case '&':
    if (peek() == '&') { advance(); return make(AND, start, startPos); }
    throw CompileError{"lexer", "unexpected '&' (did you mean '&&'?)", start};
  case '|':
    if (peek() == '|') { advance(); return make(OR, start, startPos); }
    throw CompileError{"lexer", "unexpected '|' (did you mean '||'?)", start};
  case '{': case '}': case '(': case ')': case ';': case ',':
  case '+': case '-': case '*': case '/': case '%':
    return make(c, start, startPos);
  default: {
    char buf[64];
    if (isprint(c))
      snprintf(buf, sizeof buf, "unexpected character '%c'", c);
    else
      snprintf(buf, sizeof buf, "unexpected byte 0x%02x", c);
    throw CompileError{"lexer", buf, start};
  }
  }
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> out;
  for (;;) {
    out.push_back(next());
    if (out.back().type == EOF_TOK) return out;
  }
}
