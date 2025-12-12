#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <string.h>
#include <string>
using std::string;


#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::sys;

FILE *pFile;

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns one of these for known things.
enum TOKEN_TYPE {

  IDENT = -1,        // [a-zA-Z_][a-zA-Z_0-9]*
  ASSIGN = int('='), // '='

  // delimiters
  LBRA = int('{'),  // left brace
  RBRA = int('}'),  // right brace
  LPAR = int('('),  // left parenthesis
  RPAR = int(')'),  // right parenthesis
  SC = int(';'),    // semicolon
  COMMA = int(','), // comma

  // types
  INT_TOK = -2,   // "int"
  VOID_TOK = -3,  // "void"
  FLOAT_TOK = -4, // "float"
  BOOL_TOK = -5,  // "bool"

  // keywords
  EXTERN = -6,  // "extern"
  IF = -7,      // "if"
  ELSE = -8,    // "else"
  WHILE = -9,   // "while"
  RETURN = -10, // "return"
  // TRUE   = -12,     // "true"
  // FALSE   = -13,     // "false"

  // literals
  INT_LIT = -14,   // [0-9]+
  FLOAT_LIT = -15, // [0-9]+.[0-9]+
  BOOL_LIT = -16,  // "true" or "false" key words

  // logical operators
  AND = -17, // "&&"
  OR = -18,  // "||"

  // operators
  PLUS = int('+'),    // addition or unary plus
  MINUS = int('-'),   // substraction or unary negative
  ASTERIX = int('*'), // multiplication
  DIV = int('/'),     // division
  MOD = int('%'),     // modular
  NOT = int('!'),     // unary negation

  // comparison operators
  EQ = -19,      // equal
  NE = -20,      // not equal
  LE = -21,      // less than or equal to
  LT = int('<'), // less than
  GE = -23,      // greater than or equal to
  GT = int('>'), // greater than

  // special tokens
  EOF_TOK = 0, // signal end of file

  // invalid
  INVALID = -100 // signal invalid token
};

// TOKEN struct is used to keep track of information about a token
struct TOKEN {
  int type = -100;
  std::string lexeme;
  int lineNo;
  int columnNo;
};

static std::string IdentifierStr; // Filled in if IDENT
static int IntVal;                // Filled in if INT_LIT
static bool BoolVal;              // Filled in if BOOL_LIT
static float FloatVal;            // Filled in if FLOAT_LIT
static std::string StringVal;     // Filled in if String Literal
static int lineNo, columnNo;

static TOKEN returnTok(std::string lexVal, int tok_type) {
  TOKEN return_tok;
  return_tok.lexeme = lexVal;
  return_tok.type = tok_type;
  return_tok.lineNo = lineNo;
  return_tok.columnNo = columnNo - lexVal.length() - 1;
  return return_tok;
}

// Read file line by line -- or look for \n and if found add 1 to line number
// and reset column number to 0
/// gettok - Return the next token from standard input.
static TOKEN gettok() {

  static int LastChar = ' ';
  static int NextChar = ' ';

  // Skip any whitespace.
  while (isspace(LastChar)) {
    if (LastChar == '\n' || LastChar == '\r') {
      lineNo++;
      columnNo = 1;
    }
    LastChar = getc(pFile);
    columnNo++;
  }

  if (isalpha(LastChar) ||
      (LastChar == '_')) { // identifier: [a-zA-Z_][a-zA-Z_0-9]*
    IdentifierStr = LastChar;
    columnNo++;

    while (isalnum((LastChar = getc(pFile))) || (LastChar == '_')) {
      IdentifierStr += LastChar;
      columnNo++;
    }

    if (IdentifierStr == "int")
      return returnTok("int", INT_TOK);
    if (IdentifierStr == "bool")
      return returnTok("bool", BOOL_TOK);
    if (IdentifierStr == "float")
      return returnTok("float", FLOAT_TOK);
    if (IdentifierStr == "void")
      return returnTok("void", VOID_TOK);
    if (IdentifierStr == "bool")
      return returnTok("bool", BOOL_TOK);
    if (IdentifierStr == "extern")
      return returnTok("extern", EXTERN);
    if (IdentifierStr == "if")
      return returnTok("if", IF);
    if (IdentifierStr == "else")
      return returnTok("else", ELSE);
    if (IdentifierStr == "while")
      return returnTok("while", WHILE);
    if (IdentifierStr == "return")
      return returnTok("return", RETURN);
    if (IdentifierStr == "true") {
      BoolVal = true;
      return returnTok("true", BOOL_LIT);
    }
    if (IdentifierStr == "false") {
      BoolVal = false;
      return returnTok("false", BOOL_LIT);
    }

    return returnTok(IdentifierStr.c_str(), IDENT);
  }

  if (LastChar == '=') {
    NextChar = getc(pFile);
    if (NextChar == '=') { // EQ: ==
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok("==", EQ);
    } else {
      LastChar = NextChar;
      columnNo++;
      return returnTok("=", ASSIGN);
    }
  }

  if (LastChar == '{') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok("{", LBRA);
  }
  if (LastChar == '}') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok("}", RBRA);
  }
  if (LastChar == '(') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok("(", LPAR);
  }
  if (LastChar == ')') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok(")", RPAR);
  }
  if (LastChar == ';') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok(";", SC);
  }
  if (LastChar == ',') {
    LastChar = getc(pFile);
    columnNo++;
    return returnTok(",", COMMA);
  }

  if (isdigit(LastChar) || LastChar == '.') { // Number: [0-9]+.
    std::string NumStr;

    if (LastChar == '.') { // Floatingpoint Number: .[0-9]+
      do {
        NumStr += LastChar;
        LastChar = getc(pFile);
        columnNo++;
      } while (isdigit(LastChar));

      FloatVal = strtof(NumStr.c_str(), nullptr);
      return returnTok(NumStr, FLOAT_LIT);
    } else {
      do { // Start of Number: [0-9]+
        NumStr += LastChar;
        LastChar = getc(pFile);
        columnNo++;
      } while (isdigit(LastChar));

      if (LastChar == '.') { // Floatingpoint Number: [0-9]+.[0-9]+)
        do {
          NumStr += LastChar;
          LastChar = getc(pFile);
          columnNo++;
        } while (isdigit(LastChar));

        FloatVal = strtof(NumStr.c_str(), nullptr);
        return returnTok(NumStr, FLOAT_LIT);
      } else { // Integer : [0-9]+
        IntVal = strtod(NumStr.c_str(), nullptr);
        return returnTok(NumStr, INT_LIT);
      }
    }
  }

  if (LastChar == '&') {
    NextChar = getc(pFile);
    if (NextChar == '&') { // AND: &&
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok("&&", AND);
    } else {
      LastChar = NextChar;
      columnNo++;
      return returnTok("&", int('&'));
    }
  }

  if (LastChar == '|') {
    NextChar = getc(pFile);
    if (NextChar == '|') { // OR: ||
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok("||", OR);
    } else {
      LastChar = NextChar;
      columnNo++;
      return returnTok("|", int('|'));
    }
  }

  if (LastChar == '!') {
    NextChar = getc(pFile);
    if (NextChar == '=') { // NE: !=
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok("!=", NE);
    } else {
      LastChar = NextChar;
      columnNo++;
      return returnTok("!", NOT);
      ;
    }
  }

  if (LastChar == '<') {
    NextChar = getc(pFile);
    if (NextChar == '=') { // LE: <=
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok("<=", LE);
    } else {
      LastChar = NextChar;
      columnNo++;
      return returnTok("<", LT);
    }
  }

  if (LastChar == '>') {
    NextChar = getc(pFile);
    if (NextChar == '=') { // GE: >=
      LastChar = getc(pFile);
      columnNo += 2;
      return returnTok(">=", GE);
    } else {
      LastChar = NextChar;
      columnNo++;
      return returnTok(">", GT);
    }
  }

  if (LastChar == '/') { // could be division or could be the start of a comment
    LastChar = getc(pFile);
    columnNo++;
    if (LastChar == '/') { // definitely a comment
      do {
        LastChar = getc(pFile);
        columnNo++;
      } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

      if (LastChar != EOF)
        return gettok();
    } else
      return returnTok("/", DIV);
  }

  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF) {
    columnNo++;
    return returnTok("0", EOF_TOK);
  }

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  std::string s(1, ThisChar);
  LastChar = getc(pFile);
  columnNo++;
  return returnTok(s, int(ThisChar));
}

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
static TOKEN CurTok;
static std::deque<TOKEN> tok_buffer;

static TOKEN getNextToken() {

  if (tok_buffer.size() == 0)
    tok_buffer.push_back(gettok());

  TOKEN temp = tok_buffer.front();
  tok_buffer.pop_front();

  return CurTok = temp;
}

static void putBackToken(TOKEN tok) { tok_buffer.push_front(tok); }

//===----------------------------------------------------------------------===//
// AST nodes
//===----------------------------------------------------------------------===//
///Indentation 

#include <string>

static int indentLevel = 0;
static int indentAmount = 3;
static bool cont = true;

void increaseIndentLevel() {
    indentLevel = indentLevel + indentAmount;
}

void decreaseIndentLevel() {
    indentLevel = (indentLevel > 0) ? indentLevel - indentAmount : indentLevel;
}
std::string addIndent()
{
    increaseIndentLevel();
    std::string final = "";
    if(cont == true)
        for(int i = 0; i < indentLevel; i++)
            if(i % 2 == 0)
                final.append("|");
            else
            {
                if(i == indentLevel - 1)
                    final.append("-");
                else
                    final.append(" ");
            }
    else
        final = std::string(indentLevel,' ');
    return final;
}


void resetIndent()
{
    indentLevel = 0;
}



/// ASTnode - Base class for all AST nodes.
class ASTnode {
public:
  virtual ~ASTnode() {}
  virtual Value *codegen() = 0;
  virtual std::string to_string() const {return "";};
  virtual std::string getName() const {return "";};
  virtual TOKEN getTok() const {return {};};
};

//Log Error AST node 

std::unique_ptr<ASTnode> LogError(TOKEN tok, const char *Str) {
  fprintf(stderr, "Error at line %d, column %d: %s\n", tok.lineNo, tok.columnNo, Str);
  return nullptr;
}


std::unique_ptr<ASTnode> LogErrorString(const char *Str) {
  fprintf(stderr, "Error: %s\n", Str);
  exit(2);
  return nullptr;
}


/// IntASTnode - Class for integer literals like 1, 2, 10,
class IntASTnode : public ASTnode {
  int Val;
  TOKEN Tok;
  std::string Name;

public:
  IntASTnode(TOKEN tok, int val) : Val(val), Tok(tok) {}
  virtual Value *codegen() override;
  // virtual std::string to_string() const override {
  // return a sting representation of this AST node
  //};
};

// FloatASTnode - class for float literals 
class FloatASTnode : public ASTnode {
  float Val;
  TOKEN Tok;
  std::string Name; 

public:

  FloatASTnode(TOKEN tok, float val) : Val(val), Tok(tok) {} 
  virtual Value *codegen() override; 

  virtual std::string to_string() const override {
  std::string string_representation = "FloatLiteral: " + std::to_string(Val);
  return string_representation;
  };
};

class BoolASTnode : public ASTnode {
  bool Val;
  TOKEN Tok;
  std::string Name;

public :
  BoolASTnode(TOKEN tok, bool val) : Val(val), Tok(tok) {}
  virtual Value *codegen() override; 

  virtual std::string to_string() const override {
  std::string string_representation = "BooleanLiteral: " + std::to_string(Val);
  return string_representation; 
  };

};

//identifier is a function call or variable 
 
class VariableASTnode : public ASTnode{
  TOKEN Tok;
  string Val;
  string Type;

  public:
  VariableASTnode(TOKEN tok, string type, string val) : Type(type), Val(val), Tok(tok) {}
  string getVal()
  {
    return Val;
  }
  string getType()
  {
    return Type;
  }
  virtual Value *codegen() override;
  virtual TOKEN getTok() const override{
    return Tok;
  }
  virtual std::string to_string() const override {
  //return a string representation of this AST node
    string final =  "VarDecl: " + Type + " " + Val; 
    decreaseIndentLevel();
    return final;
  };
};



class ExternDeclarationASTNode : public ASTnode {
public:
    // Attributes specific to extern declarations
    std::string name;
    std::string type;

    Value* codegen() override {
        
        return nullptr;
    }

    std::string to_string() const override {
        return "ExternDeclaration: " + name + " of type " + type;
    }
};

class ExternDeclarationListASTNode : public ASTnode {
public:
    std::vector<std::unique_ptr<ASTnode>> declarations;

    Value* codegen() override {
        // Implement code generation logic for a list of extern declarations
        return nullptr;
    }

    std::string to_string() const override {
        return "ExternDeclarationList with " + std::to_string(declarations.size()) + " items";
    }
};




/// UnaryASTnode - Expression class for a unary operator, like ! or - (check production `rval_two`)
class UnaryASTnode : public ASTnode {
  string Opcode;
  std::unique_ptr<ASTnode> Operand;
  TOKEN Tok;

public:
  UnaryASTnode(string Opcode, std::unique_ptr<ASTnode> Operand, TOKEN tok)
      : Opcode(Opcode), Operand(std::move(Operand)), Tok(tok) {}

  virtual Value *codegen() override;
  virtual std::string to_string() const override {
  //return a string representation of this AST node
    string final =  "UnaryExpr: " + Opcode  + "\n" + addIndent() + "--> " + Operand->to_string();
    decreaseIndentLevel();
    return final;
  };
};

class BinaryASTnode : public ASTnode {

  TOKEN Op;
  std::unique_ptr<ASTnode> Left, Right;
  TOKEN Tok; 

  //constructor for the BinaryASTnode Class 
  public: BinaryASTnode(TOKEN op, std::unique_ptr<ASTnode> left, std::unique_ptr<ASTnode> right) :
  Op(op), Left(std::move(left)), Right(std::move(right)), Tok(tok) {}

  //codegen function for the BinaryASTnode class
  virtual Value *codegen() override;    //codegen for the left and right nodes

  //to_string function for the BinaryASTnode class 
virtual std::string to_string() const override { 
    std::string string_representation = "BinaryExpr: " + Op + "\n" + 
                                        addIndent() + "-->" + Left->to_string() + "\n" + 
                                        addIndent() + "-->" + Right->to_string();
    decreaseIndentLevel();
    return string_representation;
}
};
/// FloatASTnode - class for float literals


/* add other AST nodes as nessasary */





std::vector<TOKEN_TYPE> FIRST_program{EXTERN, INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK};
std::vector<TOKEN_TYPE> FIRST_extern_list{EXTERN};
std::vector<TOKEN_TYPE> FIRST_extern_list_prime{EXTERN};
std::vector<TOKEN_TYPE> FIRST_extern{EXTERN};
std::vector<TOKEN_TYPE> FIRST_decl_list{INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK};
std::vector<TOKEN_TYPE> FIRST_decl_list_prime{INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK};
std::vector<TOKEN_TYPE> FIRST_decl{INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK};
std::vector<TOKEN_TYPE> FIRST_var_decl{INT_TOK, FLOAT_TOK, BOOL_TOK};
std::vector<TOKEN_TYPE> FIRST_type_spec{VOID_TOK, INT_TOK, FLOAT_TOK, BOOL_TOK};
std::vector<TOKEN_TYPE> FIRST_var_type{INT_TOK, FLOAT_TOK, BOOL_TOK};
std::vector<TOKEN_TYPE> FIRST_fun_decl{VOID_TOK, INT_TOK, FLOAT_TOK, BOOL_TOK};
std::vector<TOKEN_TYPE> FIRST_params{INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK};
std::vector<TOKEN_TYPE> FIRST_param_list{INT_TOK, FLOAT_TOK, BOOL_TOK};
std::vector<TOKEN_TYPE> FIRST_param_list_prime{COMMA};
std::vector<TOKEN_TYPE> FIRST_param{INT_TOK, FLOAT_TOK, BOOL_TOK};
std::vector<TOKEN_TYPE> FIRST_block{LBRA};
std::vector<TOKEN_TYPE> FIRST_local_decls{INT_TOK, FLOAT_TOK, BOOL_TOK};
std::vector<TOKEN_TYPE> FIRST_local_decls_prime{INT_TOK, FLOAT_TOK, BOOL_TOK};
std::vector<TOKEN_TYPE> FIRST_local_decl{INT_TOK, FLOAT_TOK, BOOL_TOK};
std::vector<TOKEN_TYPE> FIRST_stmt_list{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC};
std::vector<TOKEN_TYPE> FIRST_stmt{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC};
std::vector<TOKEN_TYPE> FIRST_expr_stmt{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, SC};
std::vector<TOKEN_TYPE> FIRST_while_stmt{WHILE};
std::vector<TOKEN_TYPE> FIRST_if_stmt{IF};
std::vector<TOKEN_TYPE> FIRST_else_stmt{ELSE};
std::vector<TOKEN_TYPE> FIRST_return_stmt{RETURN};
std::vector<TOKEN_TYPE> FIRST_expr{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, MINUS, NOT};
std::vector<TOKEN_TYPE> FIRST_or_expr{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, MINUS, NOT};
std::vector<TOKEN_TYPE> FIRST_or_expr_prime{OR};
std::vector<TOKEN_TYPE> FIRST_and_expr{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, MINUS, NOT};
std::vector<TOKEN_TYPE> FIRST_and_expr_prime{AND};
std::vector<TOKEN_TYPE> FIRST_equality_expr{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, MINUS, NOT};
std::vector<TOKEN_TYPE> FIRST_equality_expr_prime{EQ, NE};
std::vector<TOKEN_TYPE> FIRST_relational_expr{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, MINUS, NOT};
std::vector<TOKEN_TYPE> FIRST_relational_expr_prime{LT, LE, GT, GE};
std::vector<TOKEN_TYPE> FIRST_additive_expr{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, MINUS, NOT};
std::vector<TOKEN_TYPE> FIRST_additive_expr_prime{PLUS, MINUS};
std::vector<TOKEN_TYPE> FIRST_multiplicative_expr{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, MINUS, NOT};
std::vector<TOKEN_TYPE> FIRST_multiplicative_expr_prime{ASTERIX, DIV, MOD};
std::vector<TOKEN_TYPE> FIRST_unary_expr{MINUS, NOT, IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT};
std::vector<TOKEN_TYPE> FIRST_primary{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT};
std::vector<TOKEN_TYPE> FIRST_args_list{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, MINUS, NOT};
std::vector<TOKEN_TYPE> FIRST_args_list_prime{COMMA};



std::vector<TOKEN_TYPE> FOLLOW_program{EOF_TOK};
std::vector<TOKEN_TYPE> FOLLOW_extern_list{INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK};
std::vector<TOKEN_TYPE> FOLLOW_extern_list_prime{INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK};
std::vector<TOKEN_TYPE> FOLLOW_extern{EXTERN, INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK};
std::vector<TOKEN_TYPE> FOLLOW_decl_list{EOF_TOK};
std::vector<TOKEN_TYPE> FOLLOW_decl_list_prime{EOF_TOK};
std::vector<TOKEN_TYPE> FOLLOW_decl{INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK, EOF_TOK};
std::vector<TOKEN_TYPE> FOLLOW_var_decl{INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK, EOF_TOK};
std::vector<TOKEN_TYPE> FOLLOW_type_spec{IDENT};
std::vector<TOKEN_TYPE> FOLLOW_var_type{IDENT};
std::vector<TOKEN_TYPE> FOLLOW_fun_decl{INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK, EOF_TOK};
std::vector<TOKEN_TYPE> FOLLOW_params{RPAR};
std::vector<TOKEN_TYPE> FOLLOW_param_list{RPAR};
std::vector<TOKEN_TYPE> FOLLOW_param_list_prime{RPAR};
std::vector<TOKEN_TYPE> FOLLOW_param{COMMA, RPAR};
std::vector<TOKEN_TYPE> FOLLOW_block{ELSE, INT_TOK, FLOAT_TOK, BOOL_TOK, VOID_TOK, EOF_TOK, IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC, RBRA};
std::vector<TOKEN_TYPE> FOLLOW_local_decls{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC, RBRA};
std::vector<TOKEN_TYPE> FOLLOW_local_decls_prime{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC, RBRA};
std::vector<TOKEN_TYPE> FOLLOW_local_decl{INT_TOK, FLOAT_TOK, BOOL_TOK, IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC, RBRA};
std::vector<TOKEN_TYPE> FOLLOW_stmt_list{RBRA};
std::vector<TOKEN_TYPE> FOLLOW_stmt{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC, RBRA};
std::vector<TOKEN_TYPE> FOLLOW_expr_stmt{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC, RBRA};
std::vector<TOKEN_TYPE> FOLLOW_while_stmt{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC, RBRA};
std::vector<TOKEN_TYPE> FOLLOW_if_stmt{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC, RBRA};
std::vector<TOKEN_TYPE> FOLLOW_else_stmt{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC, RBRA};
std::vector<TOKEN_TYPE> FOLLOW_return_stmt{IDENT, LPAR, INT_LIT, FLOAT_LIT, BOOL_LIT, LBRA, IF, WHILE, RETURN, SC, RBRA};
std::vector<TOKEN_TYPE> FOLLOW_expr{SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_or_expr{ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_or_expr_prime{ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_and_expr{OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_and_expr_prime{OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_equality_expr{AND, OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_equality_expr_prime{AND, OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_relational_expr{EQ, NE, AND, OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_relational_expr_prime{EQ, NE, AND, OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_additive_expr{LT, LE, GT, GE, EQ, NE, AND, OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_additive_expr_prime{LT, LE, GT, GE, EQ, NE, AND, OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_multiplicative_expr{PLUS, MINUS, LT, LE, GT, GE, EQ, NE, AND, OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_multiplicative_expr_prime{PLUS, MINUS, LT, LE, GT, GE, EQ, NE, AND, OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_unary_expr{ASTERIX, DIV, MOD, PLUS, MINUS, LT, LE, GT, GE, EQ, NE, AND, OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_primary{ASTERIX, DIV, MOD, PLUS, MINUS, LT, LE, GT, GE, EQ, NE, AND, OR, ASSIGN, SC, RPAR, COMMA};
std::vector<TOKEN_TYPE> FOLLOW_args_list{RPAR};
std::vector<TOKEN_TYPE> FOLLOW_args_list_prime{RPAR};


//===----------------------------------------------------------------------===//
// Recursive Descent Parser - Function call for each production
//===----------------------------------------------------------------------===//

/* Add function calls for each production */


bool in_set(int type, std::vector<TOKEN_TYPE> list)

{
  for(int i = 0; i < list.size(); i++)
  {
    if(list[i] == type)
      return true;
  }
  return false;
}


//Function that is used to check if CurTok matches the input type, then consumes the token.
bool match_and_eat(TOKEN_TYPE token)
{
  if(CurTok.type == token)
  {
    getNextToken(); //consume token
    return true;
  }
  else
    return false;
}


//Used to determine precedence of input operator
int getPrecedence(string op)
{
  if(op == "*" | op == "/" | op == "%") //highest precedence
    return 70;
  else if(op == "+" | op == "-")
    return 60;
  else if(op == "<=" | op == "<" | op == ">=" | op == ">")
    return 50;
  else if(op == "==" | op == "!=")
    return 40;
  else if(op == "&&")
    return 30;
  else if(op == "||")
    return 20;
  else if(op == "=") //lowest precedence
    return 10;
  else
    return 110; //invalid (not an operator)
}


// program ::= extern_list decl_list
static void parser() {
if (CurTok.type == EOF_TOK) return;
parse_extern_list();
if (CurTok.type == EOF_TOK) return;
parse_decl_list();
if (CurTok.type == EOF_TOK) return;
}


//extern_list ->  extern extern_list_prime
std::vector<std::unique_ptr<ASTnode>> parse_extern_list() {

  std::vector<std::unique_ptr<ASTnode>> extern_list;


  //if matches extern parse the extern 
  if(CurTok.type == EXTERN) {

    auto extern_node = parse_extern();

    if (extern_node) {
      extern_list.push_back(extern_node);
      auto extern_list_vector = parse_extern_list_prime();
  }
} 

//return external_list
}

std::unique_ptr<ASTnode> parse_extern_list_prime() {} 

std::unique_ptr<ASTnode> parse_extern() {} 
std::unique_ptr<ASTnode> parse_decl_list() {} 
std::unique_ptr<ASTnode> parse_decl_list_prime() {} 
std::unique_ptr<ASTnode> parse_decl() {} 
std::unique_ptr<ASTnode> parse_var_decl() {} 
std::unique_ptr<ASTnode> parse_type_spec() {} 
std::unique_ptr<ASTnode> parse_var_type() {} 
std::unique_ptr<ASTnode> parse_fun_decl() {} 
std::unique_ptr<ASTnode> parse_params() {} 
std::unique_ptr<ASTnode> parse_param_list() {} 
std::unique_ptr<ASTnode> parse_param_list_prime() {} 
std::unique_ptr<ASTnode> parse_param() {} 
std::unique_ptr<ASTnode> parse_block() {} 
std::unique_ptr<ASTnode> parse_local_decls() {} 
std::unique_ptr<ASTnode> parse_local_decls_prime() {} 
std::unique_ptr<ASTnode> parse_local_decl() {} 
std::unique_ptr<ASTnode> parse_stmt_list() {} 
std::unique_ptr<ASTnode> parse_stmt() {} 
std::unique_ptr<ASTnode> parse_expr_stmt() {} 
std::unique_ptr<ASTnode> parse_while_stmt() {} 
std::unique_ptr<ASTnode> parse_if_stmt() {} 
std::unique_ptr<ASTnode> parse_else_stmt() {} 
std::unique_ptr<ASTnode> parse_return_stmt() {} 
std::unique_ptr<ASTnode> parse_expr() {} 
std::unique_ptr<ASTnode> parse_or_expr() {} 
std::unique_ptr<ASTnode> parse_or_expr_prime() {} 
std::unique_ptr<ASTnode> parse_and_expr() {} 
std::unique_ptr<ASTnode> parse_and_expr_prime() {} 
std::unique_ptr<ASTnode> parse_equality_expr() {} 
std::unique_ptr<ASTnode> parse_equality_expr_prime() {} 
std::unique_ptr<ASTnode> parse_relational_expr() {} 
std::unique_ptr<ASTnode> parse_relational_expr_prime() {} 

//additive_expr ->  multiplicative_expr additive_expr_prime
std::unique_ptr<BinaryASTnode> parse_additive_expr() {

  auto left = parse_multiplicative_expr();
  if (!left) {
      return nullptr; 
  }

  return parse_additive_expr_prime(std::move(left));
} 

//additive_expr_prime ->  ("+" | "-") multiplicative_expr additive_expr_prime  | epsilon
std::unique_ptr<BinaryASTnode> parse_additive_expr_prime(std::unique_ptr<ASTnode> left) {

  if (CurTok == PLUS || CurTok == MINUS ) {
    // Store the operator
    TOKEN op = CurTok;

    
    // Move to the next token
    if (!getNextToken()) {
        LogErrorString("Unexpected end of input after additive operator");
        return nullptr;
    }
    
    // Parse the right-hand side (another multiplicative expression)
    auto right = parse_multiplicative_expr();
    
    // Check if parsing the right-hand side was successful
    if (!right) {
        // Error was likely logged in parse_multiplicative_expr, but we can add an additional message
        LogErrorString("Failed to parse right-hand side of additive expression");
        return nullptr;
    }
    
    // Create a new binary operation node with the left, operator, and right
    auto node = std::make_unique<BinaryASTnode>(op, std::move(left), std::move(right));
    
    // Recursively parse any remaining part of the additive expression
    return parse_additive_expr_prime(std::move(node));
  }

  //epsilon case
  if (in_set(CurTok.type, FOLLOW_additive_expr_prime)) {
    return left; //return the left as it is 
  } else {
    LogError(CurTok, ("Unexpected token '" + CurTok.lexeme + "' in additive expression").c_str());
    return nullptr;
  }
}
 

std::unique_ptr<BinaryASTnode> parse_multiplicative_expr() {} 
std::unique_ptr<ASTnode> parse_multiplicative_expr_prime() {} 

// unary_expr ->  "-" unary_expr.
//              | "!" unary_expr

//              | primary

std::unique_ptr<ASTnode> parse_unary() {
  

  if (CurTok.type == NOT) {
    geNextToken();
    auto operand = parse_primary():  //recursively parse the next unary, assume that this will be primary 

    if (unary) {
      return std::make_unique<UnaryExprAST>(NOT, std::move(unary), Curtok); 
    } else {
      return nullptr;
    }
} 

if (CurTok.type == IDENT) {
    getNextToken();
    //pushToken
    //decide if i have to produce a variable or a function//this has to be LL(2)

    

    ///after i get not or minus, 
}

}


// primary ->  IDENT

//           | IDENT "(" args ")" 

//           | INT_LIT

//           | FLOAT_LIT

//           | BOOL_LIT

//           | "(" expr ")"

std::unique_ptr<ASTnode> parse_primary() {

if(!in_set(CurTok.type, FIRST_primary)) {
  LogError(CurTok, "Unexpected token in primary expression");
  return nullptr;
} 

} 

//args_list ->  expr args_list_prime

//TO DO change return type to vector of ASTnodes 
std::unique_ptr<ASTnode> parse_args_list() {

} 

std::unique_ptr<ASTnode> parse_args_list_prime() {} 





//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

static LLVMContext TheContext;
static IRBuilder<> Builder(TheContext);
static std::unique_ptr<Module> TheModule;

//===----------------------------------------------------------------------===//
// AST Printer
//===----------------------------------------------------------------------===//

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                     const ASTnode &ast) {
  os << ast.to_string();
  return os;
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main(int argc, char **argv) {
  if (argc == 2) {
    pFile = fopen(argv[1], "r");
    if (pFile == NULL)
      perror("Error opening file");
  } else {
    std::cout << "Usage: ./code InputFile\n";
    return 1;
  }

  // initialize line number and column numbers to zero
  lineNo = 1;
  columnNo = 1;

  // get the first token
  getNextToken();
  while (CurTok.type != EOF_TOK) {
    fprintf(stderr, "Token: %s with type %d\n", CurTok.lexeme.c_str(),
            CurTok.type);
    getNextToken();
  }
  fprintf(stderr, "Lexer Finished\n");

  // Make the module, which holds all the code.
  TheModule = std::make_unique<Module>("mini-c", TheContext);

  // Run the parser now.
  parser();
  fprintf(stderr, "Parsing Finished\n");

  //********************* Start printing final IR **************************
  // Print out all of the generated code into a file called output.ll
  auto Filename = "output.ll";
  std::error_code EC;
  raw_fd_ostream dest(Filename, EC, sys::fs::OF_None);

  if (EC) {
    errs() << "Could not open file: " << EC.message();
    return 1;
  }
  // TheModule->print(errs(), nullptr); // print IR to terminal
  TheModule->print(dest, nullptr);
  //********************* End printing final IR ****************************

  fclose(pFile); // close the file that contains the code that was parsed
  return 0;
}


//where i stopped: debugging, just did 4 ast nodes and added some ident function and it seemed to have worked
