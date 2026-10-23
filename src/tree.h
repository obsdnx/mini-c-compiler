#pragma once
#include "lexer.h"
#include <memory>
#include <string>
#include <vector>

// Presentation tree shared by the concrete parse tree and the AST dump.
struct TreeNode {
  std::string label;  // e.g. "additive_expr'" or "BinaryExpr"
  std::string detail; // e.g. "'+'  : int"
  SrcLoc loc;
  bool terminal = false;
  std::vector<std::unique_ptr<TreeNode>> children;

  TreeNode *add(std::string l, std::string d = "", SrcLoc at = {}, bool term = false) {
    children.push_back(std::make_unique<TreeNode>());
    TreeNode *n = children.back().get();
    n->label = std::move(l);
    n->detail = std::move(d);
    n->loc = at;
    n->terminal = term;
    return n;
  }
};

std::string jsonEscape(const std::string &s);
std::string treeToText(const TreeNode &root);
std::string treeToJson(const TreeNode &root);
