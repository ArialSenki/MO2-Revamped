#ifndef PACKERDIALOG_H
#define PACKERDIALOG_H

#include <bsapacker/IPackerDialog.h>
#include <bsapacker/IModContext.h>
#include <bsapacker/ISettingsService.h>

#include "fluentcontentdialog.h"

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace BsaPacker
{
	/**
	 * @brief Which mod to pack and the archive's name. MO2 Revamped draws it as
	 * a Windows 11 content dialog; the dependency injection builds it with the
	 * mod context and the settings (for the note about the dummy plugin).
	 **/
	class PackerDialog : public RevampedFluent::ContentDialog, public IPackerDialog
	{
		Q_OBJECT
	public:
		PackerDialog(const IModContext* modContext, const ISettingsService* settingsService);
		~PackerDialog() override = default;

		// IPackerDialog interface
		[[nodiscard]] bool IsNewFilename() const override;
		void RefreshModList() override;
		[[nodiscard]] QString SelectedMod() const override;
		[[nodiscard]] QString SelectedPluginItem() const override;
		void UpdateNameList(const QString&) override;
		void RefreshSelectedName() override;
		int Exec() override;

	public Q_SLOTS:
		void RefreshOkButton() override;

	private:
		const IModContext* m_ModContext = nullptr;
		const ISettingsService* m_SettingsService = nullptr;

		QComboBox* m_mods;
		QListWidget* m_names;
		QLabel* m_note;
		QPushButton* m_pack;
	};
} // namespace BsaPacker

#endif // PACKERDIALOG_H
