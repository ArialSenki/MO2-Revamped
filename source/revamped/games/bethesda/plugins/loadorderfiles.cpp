// MO2 Revamped: Bethesda game framework, plugins.txt and loadorder.txt.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
#include "loadorderfiles.h"

#include <Windows.h>

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace mo2r::bethesda
{

namespace
{

  std::wstring folded(std::wstring_view s)
  {
    std::wstring out(s);
    if (!out.empty()) {
      ::CharLowerBuffW(out.data(), static_cast<DWORD>(out.size()));
    }
    return out;
  }

  // the ANSI code page encoding of a name; false if a character doesn't fit
  bool toAnsi(std::wstring_view s, std::string& out)
  {
    out.clear();
    if (s.empty()) {
      return true;
    }
    BOOL lossy  = FALSE;
    const int n = ::WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, s.data(),
                                        static_cast<int>(s.size()), nullptr, 0, nullptr,
                                        &lossy);
    if (n <= 0 || lossy) {
      return false;
    }
    out.resize(n);
    ::WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, s.data(), static_cast<int>(s.size()),
                          out.data(), n, nullptr, &lossy);
    return !lossy;
  }

  std::string toUtf8(std::wstring_view s)
  {
    if (s.empty()) {
      return {};
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n,
                          nullptr, nullptr);
    return out;
  }

  std::wstring decode(UINT codePage, std::string_view s)
  {
    if (s.empty()) {
      return {};
    }
    const int n =
        ::MultiByteToWideChar(codePage, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    ::MultiByteToWideChar(codePage, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
  }

  std::string_view trim(std::string_view s)
  {
    const auto space = [](char c) {
      return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!s.empty() && space(s.front())) {
      s.remove_prefix(1);
    }
    while (!s.empty() && space(s.back())) {
      s.remove_suffix(1);
    }
    return s;
  }

  template <typename F>
  void forEachLine(std::string_view bytes, F&& f)
  {
    // a UTF-8 BOM some editors add
    if (bytes.substr(0, 3) == "\xEF\xBB\xBF") {
      bytes.remove_prefix(3);
    }
    while (!bytes.empty()) {
      const auto nl   = bytes.find('\n');
      const auto line = bytes.substr(0, nl);
      bytes.remove_prefix(nl == std::string_view::npos ? bytes.size() : nl + 1);
      const auto t = trim(line);
      if (!t.empty() && t.front() != '#') {
        f(t);
      }
    }
  }

}  // namespace

bool sameName(std::wstring_view a, std::wstring_view b)
{
  return a.size() == b.size() && folded(a) == folded(b);
}

std::vector<std::wstring> completeLoadOrder(const std::vector<std::wstring>& primary,
                                            const std::vector<std::wstring>& listed,
                                            const std::vector<std::wstring>& present)
{
  std::vector<std::wstring> out;
  out.reserve(primary.size() + listed.size() + present.size());
  std::unordered_set<std::wstring> placed;
  const auto place = [&](const std::wstring& name) {
    if (placed.insert(folded(name)).second) {
      out.push_back(name);
    }
  };
  for (const auto& name : primary) {
    place(name);
  }
  for (const auto& name : listed) {
    place(name);
  }
  std::vector<std::pair<std::wstring, std::wstring>> rest;  // (folded, name)
  for (const auto& name : present) {
    std::wstring key = folded(name);
    if (!placed.contains(key)) {
      rest.emplace_back(std::move(key), name);
    }
  }
  std::sort(rest.begin(), rest.end());
  for (const auto& [key, name] : rest) {
    if (placed.insert(key).second) {
      out.push_back(name);
    }
  }
  return out;
}

WrittenList writePluginsTxt(const std::vector<PluginState>& order,
                            const std::vector<std::wstring>& primaryPlugins,
                            PluginsTxtStyle style)
{
  std::unordered_set<std::wstring> primary;
  for (const auto& p : primaryPlugins) {
    primary.insert(folded(p));
  }
  WrittenList out;
  out.bytes.assign(MO2Header);
  std::string encoded;
  for (const auto& plugin : order) {
    if (style == PluginsTxtStyle::Asterisk) {
      if (primary.contains(folded(plugin.name))) {
        continue;
      }
    } else if (!plugin.active) {
      continue;
    }
    if (toAnsi(plugin.name, encoded)) {
      if (style == PluginsTxtStyle::Asterisk && plugin.active) {
        out.bytes += '*';
      }
      out.bytes += encoded;
    } else {
      out.unencodable.push_back(plugin.name);
    }
    out.bytes += "\r\n";
  }
  return out;
}

std::string writeLoadOrderTxt(const std::vector<PluginState>& order)
{
  std::string out(MO2Header);
  for (const auto& plugin : order) {
    out += toUtf8(plugin.name);
    out += "\r\n";
  }
  return out;
}

std::vector<PluginState> readPluginsTxt(std::string_view bytes, PluginsTxtStyle style)
{
  std::vector<PluginState> out;
  std::unordered_set<std::wstring> seen;
  forEachLine(bytes, [&](std::string_view line) {
    bool active = style == PluginsTxtStyle::ActiveOnly;
    if (style == PluginsTxtStyle::Asterisk && line.front() == '*') {
      active = true;
      line.remove_prefix(1);
    }
    std::wstring name = decode(CP_ACP, line);
    if (!name.empty() && seen.insert(folded(name)).second) {
      out.push_back({std::move(name), active});
    }
  });
  return out;
}

std::vector<std::wstring> readLoadOrderTxt(std::string_view bytes)
{
  std::vector<std::wstring> out;
  std::unordered_set<std::wstring> seen;
  forEachLine(bytes, [&](std::string_view line) {
    std::wstring name = decode(CP_UTF8, line);
    if (!name.empty() && seen.insert(folded(name)).second) {
      out.push_back(std::move(name));
    }
  });
  return out;
}

}  // namespace mo2r::bethesda
