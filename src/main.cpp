// mccomp: Mini-C compiler driver.
//
//   mccomp prog.c                     coursework mode: writes output.ll
//   mccomp prog.c --emit=asm          any single stage to stdout (or -o file)
//   mccomp prog.c --json              every stage as JSON (used by the web visualiser)
#include "astdump.h"
#include "codegen_aarch64.h"
#include "codegen_llvm.h"
#include "parser.h"
#include "sema.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

static const char *USAGE =
    "usage: mccomp <file.c | -> [options]\n"
    "\n"
    "  (no options)        compile to LLVM IR in ./output.ll (the coursework contract)\n"
    "  --emit=<stage>      print one stage: tokens | parse-tree | ast | symbols |\n"
    "                      ir | ir-opt | llvm-asm | asm\n"
    "  --target=<os>       AArch64 flavour for asm/llvm-asm: darwin | linux (default: host)\n"
    "  -o <file>           write output to <file> instead of stdout / output.ll\n"
    "  --json              dump every stage as one JSON document\n"
    "  -h, --help          show this help\n";

struct Options {
  std::string input, emit, output, target;
  bool json = false;
};

static std::vector<std::string> splitLines(const std::string &s) {
  std::vector<std::string> lines;
  std::stringstream ss(s);
  std::string l;
  while (std::getline(ss, l)) lines.push_back(l);
  return lines;
}

static std::string symbolsText(const std::vector<SymbolRow> &rows) {
  std::string out;
  char buf[512];
  snprintf(buf, sizeof buf, "%-28s %-14s %-9s %-24s %s\n", "SCOPE", "NAME", "KIND", "TYPE", "DECLARED");
  out += buf;
  for (auto &r : rows) {
    snprintf(buf, sizeof buf, "%-28s %-14s %-9s %-24s %d:%d\n", r.scope.c_str(), r.name.c_str(), r.kind.c_str(),
             r.type.c_str(), r.loc.line, r.loc.col);
    out += buf;
  }
  return out;
}

// clang-style diagnostic with the offending line and a caret.
static void printDiagnostic(const std::string &file, const std::vector<std::string> &lines, const Diagnostic &d) {
  bool color = isatty(STDERR_FILENO);
  const char *bold = color ? "\033[1m" : "", *reset = color ? "\033[0m" : "";
  const char *sevColor = !color ? "" : d.severity == "error" ? "\033[1;31m" : "\033[1;35m";
  fprintf(stderr, "%s%s:%d:%d: %s%s:%s %s%s%s\n", bold, file.c_str(), d.loc.line, d.loc.col, sevColor,
          d.severity.c_str(), reset, bold, d.message.c_str(), reset);
  if (d.loc.line >= 1 && d.loc.line <= (int)lines.size()) {
    fprintf(stderr, "%5d | %s\n      | ", d.loc.line, lines[d.loc.line - 1].c_str());
    const std::string &l = lines[d.loc.line - 1];
    for (int i = 1; i < d.loc.col && i - 1 < (int)l.size(); i++) fputc(l[i - 1] == '\t' ? '\t' : ' ', stderr);
    fprintf(stderr, "%s^%s\n", color ? "\033[1;32m" : "", reset);
  }
}

static std::string tripleFor(const std::string &target) {
  if (target == "darwin") return "arm64-apple-macosx14.0.0";
  if (target == "linux") return "aarch64-unknown-linux-gnu";
  return "";
}

static AArch64OS osFor(const std::string &target) {
  if (target == "linux") return AArch64OS::Linux;
  if (target == "darwin") return AArch64OS::Darwin;
#ifdef __APPLE__
  return AArch64OS::Darwin;
#else
  return AArch64OS::Linux;
#endif
}

//===--- JSON mode ---------------------------------------------------------===//

static int runJson(const std::string &source, const Options &opt) {
  using Clock = std::chrono::steady_clock;
  std::string target = opt.target.empty() ? (osFor("") == AArch64OS::Darwin ? "darwin" : "linux") : opt.target;
  auto lines = splitLines(source);
  std::vector<std::pair<std::string, std::string>> fields; // key -> raw JSON
  std::vector<Diagnostic> diags;
  std::string timings;
  auto timed = [&](const char *name, auto &&fn) {
    auto t0 = Clock::now();
    fn();
    long us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
    timings += std::string(timings.empty() ? "" : ",") + jsonEscape(name) + ":" + std::to_string(us);
  };

  std::string stage = "lexer";
  try {
    // Lex incrementally so the tokens before a lexical error are still shown.
    std::string toks = "[";
    Lexer lexer(source);
    std::vector<Token> tokens;
    try {
      timed("lex", [&] {
        for (;;) {
          tokens.push_back(lexer.next());
          if (tokens.back().type == EOF_TOK) break;
        }
      });
    } catch (...) {
      for (size_t i = 0; i < tokens.size(); i++)
        toks += std::string(i ? "," : "") + "{\"kind\":" + jsonEscape(tokenKindName(tokens[i].type)) +
                ",\"lexeme\":" + jsonEscape(tokens[i].lexeme) + ",\"line\":" + std::to_string(tokens[i].loc.line) +
                ",\"col\":" + std::to_string(tokens[i].loc.col) + "}";
      fields.push_back({"tokens", toks + "]"});
      throw;
    }
    for (size_t i = 0; i < tokens.size(); i++)
      toks += std::string(i ? "," : "") + "{\"kind\":" + jsonEscape(tokenKindName(tokens[i].type)) +
              ",\"lexeme\":" + jsonEscape(tokens[i].lexeme) + ",\"line\":" + std::to_string(tokens[i].loc.line) +
              ",\"col\":" + std::to_string(tokens[i].loc.col) + "}";
    fields.push_back({"tokens", toks + "]"});

    stage = "parser";
    Parser parser(tokens, true);
    std::unique_ptr<Program> prog;
    try {
      timed("parse", [&] { prog = parser.parseProgram(); });
    } catch (...) {
      if (parser.parseTree) fields.push_back({"parseTree", treeToJson(*parser.parseTree)});
      throw;
    }
    fields.push_back({"parseTree", treeToJson(*parser.parseTree)});

    stage = "semantic";
    Sema sema;
    try {
      timed("sema", [&] { sema.run(*prog); });
    } catch (...) {
      fields.push_back({"ast", treeToJson(*dumpAST(*prog, false))});
      throw;
    }
    diags = sema.warnings;
    fields.push_back({"ast", treeToJson(*dumpAST(*prog, true))});
    std::string syms = "[";
    for (size_t i = 0; i < sema.symbols.size(); i++) {
      auto &r = sema.symbols[i];
      syms += std::string(i ? "," : "") + "{\"scope\":" + jsonEscape(r.scope) + ",\"name\":" + jsonEscape(r.name) +
              ",\"kind\":" + jsonEscape(r.kind) + ",\"type\":" + jsonEscape(r.type) +
              ",\"line\":" + std::to_string(r.loc.line) + ",\"col\":" + std::to_string(r.loc.col) + "}";
    }
    fields.push_back({"symbols", syms + "]"});

    stage = "codegen";
    LLVMOutput llvmOut;
    timed("llvm", [&] { llvmOut = generateLLVM(*prog, tripleFor(target), true, true); });
    fields.push_back({"ir", jsonEscape(llvmOut.ir)});
    fields.push_back({"irOpt", jsonEscape(llvmOut.irOpt)});
    fields.push_back({"llvmAsm", jsonEscape(llvmOut.asmText)});
    std::string mine;
    timed("aarch64", [&] { mine = generateAArch64(*prog, osFor(target), lines); });
    fields.push_back({"asm", jsonEscape(mine)});
  } catch (const CompileError &e) {
    diags.push_back({"error", e.stage, e.message, e.loc});
  }

  bool ok = true;
  std::string d = "[";
  for (size_t i = 0; i < diags.size(); i++) {
    if (diags[i].severity == "error") ok = false;
    d += std::string(i ? "," : "") + "{\"severity\":" + jsonEscape(diags[i].severity) +
         ",\"stage\":" + jsonEscape(diags[i].stage) + ",\"message\":" + jsonEscape(diags[i].message) +
         ",\"line\":" + std::to_string(diags[i].loc.line) + ",\"col\":" + std::to_string(diags[i].loc.col) + "}";
  }
  std::string out = "{\"ok\":" + std::string(ok ? "true" : "false") + ",\"target\":" + jsonEscape(target) +
                    ",\"diagnostics\":" + d + "],\"timings\":{" + timings + "}";
  for (auto &f : fields) out += "," + jsonEscape(f.first) + ":" + f.second;
  out += "}\n";
  fwrite(out.data(), 1, out.size(), stdout);
  return 0;
}

//===--- Normal mode -------------------------------------------------------===//

static int writeOut(const Options &opt, const std::string &text, const std::string &defaultPath) {
  std::string path = !opt.output.empty() ? opt.output : defaultPath;
  if (path.empty() || path == "-") {
    fwrite(text.data(), 1, text.size(), stdout);
    return 0;
  }
  std::ofstream f(path);
  if (!f) {
    fprintf(stderr, "mccomp: cannot write '%s'\n", path.c_str());
    return 1;
  }
  f << text;
  return 0;
}

static int runNormal(const std::string &source, const Options &opt) {
  auto lines = splitLines(source);
  std::string file = opt.input == "-" ? "<stdin>" : opt.input;
  const std::string &emit = opt.emit;
  try {
    auto tokens = Lexer(source).tokenize();
    if (emit == "tokens") {
      std::string out;
      char buf[256];
      for (auto &t : tokens) {
        snprintf(buf, sizeof buf, "%4d:%-4d %-10s %s\n", t.loc.line, t.loc.col, tokenKindName(t.type).c_str(),
                 t.lexeme.c_str());
        out += buf;
      }
      return writeOut(opt, out, "");
    }

    Parser parser(tokens, emit == "parse-tree");
    auto prog = parser.parseProgram();
    if (emit == "parse-tree") return writeOut(opt, treeToText(*parser.parseTree), "");

    Sema sema;
    sema.run(*prog);
    for (auto &w : sema.warnings) printDiagnostic(file, lines, w);
    if (emit == "ast") return writeOut(opt, treeToText(*dumpAST(*prog, true)), "");
    if (emit == "symbols") return writeOut(opt, symbolsText(sema.symbols), "");
    if (emit == "asm") return writeOut(opt, generateAArch64(*prog, osFor(opt.target), lines), "");

    bool needOpt = emit == "ir-opt" || emit == "llvm-asm";
    std::string triple = emit == "llvm-asm" ? tripleFor(opt.target.empty() ? (osFor("") == AArch64OS::Darwin ? "darwin" : "linux") : opt.target) : "";
    LLVMOutput out = generateLLVM(*prog, triple, needOpt, emit == "llvm-asm");
    if (emit == "ir-opt") return writeOut(opt, out.irOpt, "");
    if (emit == "llvm-asm") return writeOut(opt, out.asmText, "");
    if (emit == "ir") return writeOut(opt, out.ir, "");
    return writeOut(opt, out.ir, "output.ll");
  } catch (const CompileError &e) {
    printDiagnostic(file, lines, {"error", e.stage, e.message, e.loc});
    return 1;
  }
}

int main(int argc, char **argv) {
  Options opt;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      fputs(USAGE, stdout);
      return 0;
    } else if (a.rfind("--emit=", 0) == 0) {
      opt.emit = a.substr(7);
    } else if (a.rfind("--target=", 0) == 0) {
      opt.target = a.substr(9);
    } else if (a == "--json") {
      opt.json = true;
    } else if (a == "-o" && i + 1 < argc) {
      opt.output = argv[++i];
    } else if (opt.input.empty() && (a == "-" || a[0] != '-')) {
      opt.input = a;
    } else {
      fprintf(stderr, "mccomp: unknown argument '%s'\n\n%s", a.c_str(), USAGE);
      return 2;
    }
  }
  static const char *stages[] = {"", "tokens", "parse-tree", "ast", "symbols", "ir", "ir-opt", "llvm-asm", "asm"};
  bool validEmit = false;
  for (auto s : stages) validEmit |= opt.emit == s;
  if (opt.input.empty() || !validEmit || !(opt.target.empty() || opt.target == "darwin" || opt.target == "linux")) {
    fputs(USAGE, stderr);
    return 2;
  }

  std::stringstream buf;
  if (opt.input == "-") {
    buf << std::cin.rdbuf();
  } else {
    std::ifstream f(opt.input);
    if (!f) {
      fprintf(stderr, "mccomp: cannot open '%s'\n", opt.input.c_str());
      return 1;
    }
    buf << f.rdbuf();
  }
  return opt.json ? runJson(buf.str(), opt) : runNormal(buf.str(), opt);
}
