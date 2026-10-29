/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MO2FOLDERICONPROVIDER_H
#define MO2FOLDERICONPROVIDER_H

#include <QFileIconProvider>
#include "fluenticons.h"
#include <QFileInfo>
#include <QIcon>

// Windows' own name for a folder, in the user's language (filetreeitem.cpp)
const QString& directoryFileType();

class MO2FolderIconProvider final : public QFileIconProvider
{
public:
  // Qt calls every folder "File Folder" in English whatever the language;
  // files already get Windows' names for their types
  QString type(const QFileInfo& info) const override
  {
    if (info.isDir()) {
      return directoryFileType();
    }
    return QFileIconProvider::type(info);
  }

  QIcon icon(IconType type) const override
  {
    if (type == Folder) {
      return FluentIcons::icon(QStringLiteral(":/MO/gui/folder"));
    }
    return QFileIconProvider::icon(type);
  }

  QIcon icon(const QFileInfo& info) const override
  {
    // Fluent themes use the same file glyphs as the Data tab
    if (FluentIcons::active()) {
      return FluentIcons::file(info.fileName(), info.isDir());
    }
    if (info.isDir()) {
      return icon(Folder);
    }
    return QFileIconProvider::icon(info);
  }
};

inline QFileIconProvider* mo2FolderIconProvider()
{
  static MO2FolderIconProvider provider;
  return &provider;
}

#endif  // MO2FOLDERICONPROVIDER_H
