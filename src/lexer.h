#pragma once
#include <string>
#include <vector>

// Token kinds. Single-character tokens use their ASCII value, multi-character
// and keyword tokens use negative values (the convention from the CS325 skeleton).
enum TokenType {
  IDENT = -1,
  ASSIGN = '=',

  LBRA = '{',
  RBRA = '}',
  LPAR = '(',
  RPAR = ')',
  SC = ';',
  COMMA = ',',

  INT_TOK = -2,
  VOID_TOK = -3,
  FLOAT_TOK = -4,
  BOOL_TOK = -5,

  EXTERN = -6,
  IF = -7,
  ELSE = -8,
  WHILE = -9,
  RETURN = -10,

  INT_LIT = -14,
  FLOAT_LIT = -15,
  BOOL_LIT = -16,

  AND = -17,
  OR = -18,

  PLUS = '+',
  MINUS = '-',
  ASTERIX = '*',
  DIV = '/',
  MOD = '%',
  NOT = '!',

  EQ = -19,
  NE = -20,
  LE = -21,
  LT = '<',
  GE = -23,
  GT = '>',

  EOF_TOK = 0,
  INVALID = -100
};

struct SrcLoc {
  int line = 0;
  int col = 0;
};

struct Token {
  int type = INVALID;
  std::string lexeme;
  SrcLoc loc;
};

// Human-readable token kind, e.g. "IDENT", "'+'", "FLOAT_LIT".
std::string tokenKindName(int type);

// Thrown by every stage on the first error; carries the stage and a source position.
struct CompileError {
  std::string stage;
  std::string message;
  SrcLoc loc;
};

// Hand-written maximal-munch scanner. Works on an in-memory buffer so the same
// code serves the CLI, the test-suite and the web visualiser.
class Lexer {
public:
  explicit Lexer(std::string source) : src(std::move(source)) {}
  Token next();
  std::vector<Token> tokenize();

private:
  std::string src;
  size_t pos = 0;
  int line = 1, col = 1;

  int peek(size_t ahead = 0) const {
    return pos + ahead < src.size() ? (unsigned char)src[pos + ahead] : EOF;
  }
  int advance();
  Token make(int type, SrcLoc start, size_t startPos) const {
    return Token{type, src.substr(startPos, pos - startPos), start};
  }
};
