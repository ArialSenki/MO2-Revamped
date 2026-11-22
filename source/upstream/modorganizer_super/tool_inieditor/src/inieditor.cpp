/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of ini editor plugin for MO

Ini editor plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Ini editor plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Ini editor plugin.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "inieditor.h"

#include "iplugingame.h"
#include "textviewer.h"
#include <utility.h>
#include <imoinfo.h>
#include <pluginrequirements.h>

#include <QtPlugin>
#include <QMessageBox>
#include <QDir>
#include <QApplication>


using namespace MOBase;


IniEditor::IniEditor()
  : m_MOInfo(nullptr)
{
}

bool IniEditor::init(IOrganizer *moInfo)
{
  m_MOInfo = moInfo;
  return true;
}

// only games with .ini files have something to edit here (MO2 Revamped)
std::vector<std::shared_ptr<const MOBase::IPluginRequirement>> IniEditor::requirements() const
{
  return {MOBase::PluginRequirementFactory::basic(
      [](MOBase::IOrganizer* organizer) {
        const MOBase::IPluginGame* game = organizer->managedGame();
        return game != nullptr &&
               !game->iniFiles().filter(".ini", Qt::CaseInsensitive).isEmpty();
      },
      QObject::tr("This game has no INI files."))};
}

QString IniEditor::name() const
{
  return "INI Editor";
}

QString IniEditor::localizedName() const
{
	return tr("INI Editor");
}

QString IniEditor::author() const
{
  return "Tannin";
}

QString IniEditor::description() const
{
  return tr("Very basic editor for INI files");
}

VersionInfo IniEditor::version() const
{
  return VersionInfo(1, 1, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> IniEditor::settings() const
{
  QList<PluginSetting> result;
  result.push_back(PluginSetting("external", "Open the files in another program instead of the built-in editor.", QVariant(false)));
  result.push_back(PluginSetting("associated", "With an external editor, open the files in the program Windows uses for .ini "
                                 "files. When off, Windows' Edit command opens them, usually in Notepad.", QVariant(true)));
  return result;
}

QString IniEditor::displayName() const
{
  return tr("INI Editor");
}

QString IniEditor::tooltip() const
{
  return tr("Edit the INI files of the current profile");
}

QIcon IniEditor::icon() const
{
	return QIcon(":/inieditor/icon_document");
}

void IniEditor::display() const
{
	if (m_MOInfo == nullptr) {
		throw MyException(tr("plugin not initialized"));
	}

	QStringList iniFiles = m_MOInfo->managedGame()->iniFiles();
	if (m_MOInfo->pluginSetting(name(), "external").toBool()) {
		for (QString const &file : iniFiles) {
			QString fileName = m_MOInfo->profile()->absoluteIniFilePath(file);
			::ShellExecuteW(nullptr, m_MOInfo->pluginSetting(name(), "associated").toBool() ? L"open" : L"edit",
				ToWString(fileName).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
	}
	else {
		TextViewer *viewer = new TextViewer(tr("INI files"), parentWidget());
		if (m_MOInfo->profile()->localSettingsEnabled())
		{
			viewer->setDescription(tr("These are this profile's own INI files.\n\nTo open them in another program, turn on Use an external editor in Settings > Plugins > INI Editor."));
		}
		else {
			viewer->setDescription(tr("These are the game's INI files in Documents\\My Games, shared by the profiles without their own INI files.\n\nTo open them in another program, turn on Use an external editor in Settings > Plugins > INI Editor."));
		}
		for (QString const& file : iniFiles) {
			QString fileName = m_MOInfo->profile()->absoluteIniFilePath(file);
			QFileInfo fileInfo(fileName);
			if (fileInfo.exists()) {
				if (fileInfo.size() < 1024 * 1024) {
					viewer->addFile(fileName, true);
				}
				else {
					QMessageBox::warning(parentWidget(), tr("File too big"),
						tr("%1 is larger than 1 MB, too large for the INI Editor.").arg(fileName));
				}
			}
		}
		viewer->show();
	}
}



#if QT_VERSION < QT_VERSION_CHECK(5,0,0)
Q_EXPORT_PLUGIN2(iniEditor, IniEditor)
#endif
