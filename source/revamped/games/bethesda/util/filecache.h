// MO2 Revamped: Bethesda game framework, values cached by file size and time.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2 asks a game plugin the same things many times per refresh (primary
// plugins, the game version, ...). Anything computed from a file is kept here
// and computed again only when the file's existence, size or modification time
// changes. Thread-safe: MO2 calls game plugins from its refresh threads.
#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QString>

#include <map>
#include <mutex>
#include <utility>

namespace mo2r::bethesda
{

struct FileStamp
{
  bool exists = false;
  qint64 size = -1;
  QDateTime modified;

  static FileStamp of(const QString& path)
  {
    const QFileInfo info(path);
    FileStamp s;
    s.exists = info.exists();
    if (s.exists) {
      s.size     = info.size();
      s.modified = info.lastModified();
    }
    return s;
  }

  bool operator==(const FileStamp&) const = default;
};

template <typename T>
class FileCache
{
public:
  // the value for path, from compute() the first time and whenever the file
  // changed since
  template <typename F>
  T get(const QString& path, F&& compute) const
  {
    const FileStamp stamp = FileStamp::of(path);
    const QString key     = path.toCaseFolded();
    {
      std::scoped_lock lock(m_mutex);
      const auto it = m_entries.find(key);
      if (it != m_entries.end() && it->second.first == stamp) {
        return it->second.second;
      }
    }
    T value = compute();
    std::scoped_lock lock(m_mutex);
    m_entries.insert_or_assign(key, std::make_pair(stamp, value));
    return value;
  }

private:
  mutable std::mutex m_mutex;
  mutable std::map<QString, std::pair<FileStamp, T>> m_entries;
};

}  // namespace mo2r::bethesda
