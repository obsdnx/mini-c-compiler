#pragma once
#include "ast.h"
#include <string>

struct LLVMOutput {
  std::string ir;      // unoptimised IR (the coursework's output.ll)
  std::string irOpt;   // after the -O2 pipeline (empty unless requested)
  std::string asmText; // LLVM's own assembly for the target (empty unless requested)
};

// `triple` empty means "the host". Throws CompileError if the IR fails verification.
LLVMOutput generateLLVM(const Program &prog, const std::string &triple, bool optimise, bool emitAsm);
