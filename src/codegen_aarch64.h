#pragma once
#include "ast.h"
#include <string>
#include <vector>

enum class AArch64OS { Darwin, Linux };

// Direct AST -> AArch64 assembly backend, independent of LLVM.
// `sourceLines` is used to annotate the assembly with the originating source line.
std::string generateAArch64(const Program &prog, AArch64OS os, const std::vector<std::string> &sourceLines);
