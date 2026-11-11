#include "ModContext.h"

#include <array>
#include <functional>
#include "iplugingame.h"
#include "imodinterface.h"
#include "imodlist.h"
#include "NexusId.h"
#include <QtConcurrent/QtConcurrentFilter>
#include <QtConcurrent/QtConcurrentMap>

namespace BsaPacker
{
	const QStringList& ModContext::PLUGIN_TYPES = { "*.esm", "*.esp", "*.esl" };

	ModContext::ModContext(MOBase::IOrganizer *moInfo) : m_Organizer(moInfo)
	{
	}

	QString ModContext::GetAbsoluteModPath(const QString &modName) const
	{
		const MOBase::IModInterface* const mod = m_Organizer->modList()->getMod(modName);
		return mod->absolutePath();
	}

	int ModContext::GetNexusId() const
	{
		const MOBase::IPluginGame* managedGame = this->m_Organizer->managedGame();
		int nexusId = managedGame->nexusGameID();

		// Games with no Nexus page have an ID of 0. Use primarySources in that case
		if (nexusId != 0) {
			return nexusId;
		}
		if (!managedGame->primarySources().isEmpty()) {
			QString primarySource = managedGame->primarySources().first();
			if (primarySource == "FalloutNV") { // TTW
				return NexusId::NewVegas;
			}
		}		
		return nexusId;
	}

	QStringList ModContext::GetPlugins(const QDir& modDirectory) const
	{
		const int nexusId = this->GetNexusId();
		const QString extension = (nexusId == NexusId::Fallout4 || nexusId == NexusId::Starfield) ? ".ba2" : ".bsa";
		const std::function<QString(QString)> replace_extension = [&](QString fname) {
			return fname.replace(fname.lastIndexOf('.'), 4, extension);
		};

		QStringList filenames = QtConcurrent::blockingMapped(modDirectory.entryList(PLUGIN_TYPES, QDir::Files), replace_extension);
		filenames.removeDuplicates();
		return filenames << QStringLiteral("<new filename>") + extension;
	}

	QStringList ModContext::GetValidMods() const
	{
		const MOBase::IModList* const list = m_Organizer->modList();
		// MO2 Revamped: only mods MO2 installed. The game's own DLC and Creation
		// Club content ("foreign" mods) live in the game's folder: packing them
		// wrote an archive there and hid the game's files. Backups and Overwrite
		// aren't mods to pack either. Checked one after another: getMod isn't
		// meant for other threads.
		QStringList mods;
		for (const QString& mod : list->allMods()) {
			if (mod.endsWith("_separator", Qt::CaseInsensitive) ||
				!(list->state(mod) & MOBase::IModList::STATE_VALID)) {
				continue;
			}
			const MOBase::IModInterface* const info = list->getMod(mod);
			if (info == nullptr || info->isForeign() || info->isBackup() || info->isOverwrite() ||
				info->isSeparator()) {
				continue;
			}
			mods.append(mod);
		}
		return mods;
	}
} // namespace BsaPacker
