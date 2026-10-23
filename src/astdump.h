#pragma once
#include "ast.h"
#include "tree.h"

// Converts the AST into a printable tree. `typed` adds the types computed by sema.
std::unique_ptr<TreeNode> dumpAST(const Program &p, bool typed);
