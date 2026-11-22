// MO2 Revamped: Bethesda game framework, Steam library detection.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
#include "steamlibraries.h"

namespace mo2r::bethesda
{

namespace
{

  struct Token
  {
    enum Kind { String, Open, Close, End, Bad } kind;
    std::string text;
  };

  class Lexer
  {
  public:
    explicit Lexer(std::string_view s) : m_s(s) {}

    Token next()
    {
      skipSpaceAndComments();
      if (m_i >= m_s.size()) {
        return {Token::End, {}};
      }
      const char c = m_s[m_i];
      if (c == '{') {
        ++m_i;
        return {Token::Open, {}};
      }
      if (c == '}') {
        ++m_i;
        return {Token::Close, {}};
      }
      if (c == '"') {
        return quoted();
      }
      // an unquoted token (rare in this file): up to whitespace or a brace
      std::string text;
      while (m_i < m_s.size() && !isSpace(m_s[m_i]) && m_s[m_i] != '{' && m_s[m_i] != '}' &&
             m_s[m_i] != '"') {
        text += m_s[m_i++];
      }
      return {Token::String, std::move(text)};
    }

  private:
    static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

    void skipSpaceAndComments()
    {
      for (;;) {
        while (m_i < m_s.size() && isSpace(m_s[m_i])) {
          ++m_i;
        }
        if (m_i + 1 < m_s.size() && m_s[m_i] == '/' && m_s[m_i + 1] == '/') {
          while (m_i < m_s.size() && m_s[m_i] != '\n') {
            ++m_i;
          }
          continue;
        }
        return;
      }
    }

    Token quoted()
    {
      ++m_i;  // opening quote
      std::string text;
      while (m_i < m_s.size()) {
        const char c = m_s[m_i++];
        if (c == '"') {
          return {Token::String, std::move(text)};
        }
        if (c == '\\' && m_i < m_s.size()) {
          const char e = m_s[m_i++];
          switch (e) {
          case 'n':
            text += '\n';
            break;
          case 't':
            text += '\t';
            break;
          default:  // \\ and \" (Steam writes paths as D:\\SteamLibrary)
            text += e;
            break;
          }
          continue;
        }
        text += c;
      }
      return {Token::Bad, {}};  // unterminated
    }

    std::string_view m_s;
    std::size_t m_i = 0;
  };

  // a block's direct string values and child blocks
  struct Block
  {
    std::map<std::string, std::string> values;
    std::vector<std::pair<std::string, Block>> children;
  };

  // parses the inside of a block, after its '{'; false on malformed input
  bool parseBlock(Lexer& lex, Block& out, int depth, bool topLevel)
  {
    if (depth > 16) {
      return false;
    }
    for (;;) {
      Token key = lex.next();
      if (key.kind == Token::Close) {
        return !topLevel;
      }
      if (key.kind == Token::End) {
        return topLevel;
      }
      if (key.kind != Token::String) {
        return false;
      }
      Token value = lex.next();
      if (value.kind == Token::String) {
        out.values[key.text] = std::move(value.text);
      } else if (value.kind == Token::Open) {
        Block child;
        if (!parseBlock(lex, child, depth + 1, false)) {
          return false;
        }
        out.children.emplace_back(std::move(key.text), std::move(child));
      } else {
        return false;
      }
    }
  }

  bool equalsIgnoreCase(std::string_view a, std::string_view b)
  {
    if (a.size() != b.size()) {
      return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
      char x = a[i], y = b[i];
      if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
      if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
      if (x != y) {
        return false;
      }
    }
    return true;
  }

}  // namespace

std::vector<SteamLibrary> parseSteamLibraries(std::string_view vdf)
{
  Lexer lex(vdf);
  Block root;
  if (!parseBlock(lex, root, 0, true)) {
    return {};
  }
  std::vector<SteamLibrary> result;
  for (const auto& [name, folders] : root.children) {
    if (!equalsIgnoreCase(name, "libraryfolders")) {
      continue;
    }
    for (const auto& [index, library] : folders.children) {
      SteamLibrary lib;
      for (const auto& [k, v] : library.values) {
        if (equalsIgnoreCase(k, "path")) {
          lib.path = v;
        }
      }
      for (const auto& [childName, child] : library.children) {
        if (equalsIgnoreCase(childName, "apps")) {
          for (const auto& [app, size] : child.values) {
            lib.apps.push_back(app);
          }
        }
      }
      if (!lib.path.empty()) {
        result.push_back(std::move(lib));
      }
    }
  }
  return result;
}

std::optional<std::string> steamLibraryForApp(const std::vector<SteamLibrary>& libraries,
                                              std::string_view appId)
{
  for (const auto& lib : libraries) {
    for (const auto& app : lib.apps) {
      if (app == appId) {
        return lib.path;
      }
    }
  }
  return std::nullopt;
}

}  // namespace mo2r::bethesda
