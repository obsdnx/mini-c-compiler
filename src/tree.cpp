#include "tree.h"
#include <cstdio>

std::string jsonEscape(const std::string &s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20) {
        char buf[8];
        snprintf(buf, sizeof buf, "\\u%04x", c);
        out += buf;
      } else {
        out += (char)c;
      }
    }
  }
  return out + "\"";
}

static void textRec(const TreeNode &n, const std::string &prefix, bool last, bool root,
                    std::string &out) {
  if (!root) out += prefix + (last ? "└─ " : "├─ ");
  out += n.label;
  if (!n.detail.empty()) out += " " + n.detail;
  if (n.loc.line) out += "  <" + std::to_string(n.loc.line) + ":" + std::to_string(n.loc.col) + ">";
  out += "\n";
  std::string childPrefix = root ? "" : prefix + (last ? "   " : "│  ");
  for (size_t i = 0; i < n.children.size(); i++)
    textRec(*n.children[i], childPrefix, i + 1 == n.children.size(), false, out);
}

std::string treeToText(const TreeNode &root) {
  std::string out;
  textRec(root, "", true, true, out);
  return out;
}

static void jsonRec(const TreeNode &n, std::string &out) {
  out += "{\"label\":" + jsonEscape(n.label);
  if (!n.detail.empty()) out += ",\"detail\":" + jsonEscape(n.detail);
  if (n.loc.line) out += ",\"line\":" + std::to_string(n.loc.line) + ",\"col\":" + std::to_string(n.loc.col);
  if (n.terminal) out += ",\"terminal\":true";
  if (!n.children.empty()) {
    out += ",\"children\":[";
    for (size_t i = 0; i < n.children.size(); i++) {
      if (i) out += ",";
      jsonRec(*n.children[i], out);
    }
    out += "]";
  }
  out += "}";
}

std::string treeToJson(const TreeNode &root) {
  std::string out;
  jsonRec(root, out);
  return out;
}
