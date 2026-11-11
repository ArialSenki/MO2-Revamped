#include "PackerDialog.h"
#include "SettingsService.h"

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStyle>
#include <QStringList>

namespace BsaPacker
{
	namespace
	{
		// ModContext::GetPlugins ends the list with this item, which asks for a name
		bool isNewNameItem(const QString& text)
		{
			return text.startsWith(QStringLiteral("<new filename>"));
		}
	}

	PackerDialog::PackerDialog(const IModContext* modContext,
	                           const ISettingsService* settingsService) :
		RevampedFluent::ContentDialog(
			nullptr, QStringLiteral("BsaPackerDialog"), QObject::tr("BSA Packer"),
			QObject::tr("MO2 packs the mod's loose files into an archive named after one of "
			            "its plugins, so the game loads the archive with that plugin. The "
			            "loose files stay unless BSA Packer's settings hide them.")),
		m_ModContext(modContext),
		m_SettingsService(settingsService)
	{
		this->setHeading(QObject::tr("Pack a mod's files into an archive"));
		this->setMinimumWidth(480);

		this->m_mods = new QComboBox(this);
		this->body()->addWidget(RevampedFluent::fieldLabel(QObject::tr("Mod"), this->m_mods, this));
		this->body()->addWidget(this->m_mods);

		this->m_names = new QListWidget(this);
		this->body()->addSpacing(4);
		this->body()->addWidget(RevampedFluent::fieldLabel(QObject::tr("Archive name"), this->m_names, this));
		this->m_names->setSelectionMode(QAbstractItemView::SingleSelection);
		this->m_names->setMinimumHeight(140);
		this->body()->addWidget(this->m_names, 1);

		this->m_note = RevampedFluent::problemLabel(QString(), this);
		this->body()->addWidget(this->m_note);

		this->m_pack = this->addButton(QObject::tr("Pack"), true);
		QPushButton* cancel = this->addButton(QObject::tr("Cancel"));

		QObject::connect(this->m_pack, &QPushButton::clicked, this, [this] {
			if (this->m_pack->isEnabled()) {
				this->accept();
			}
		});
		QObject::connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
		// the selection, not only the current row: assistive tools select an
		// item without making it current
		QObject::connect(this->m_names, &QListWidget::itemSelectionChanged, this, [this] {
			const auto selected = this->m_names->selectedItems();
			if (selected.size() == 1 && selected.first() != this->m_names->currentItem()) {
				this->m_names->setCurrentItem(selected.first(), QItemSelectionModel::NoUpdate);
			}
			this->RefreshOkButton();
		});
		QObject::connect(this->m_names, &QListWidget::itemDoubleClicked, this, [this] {
			if (this->m_pack->isEnabled()) {
				this->accept();
			}
		});
		QObject::connect(this->m_mods, &QComboBox::currentTextChanged, this, [this](const QString& text) {
			this->UpdateNameList(text);
		});

		this->setTabOrder(this->m_mods, this->m_names);
		this->setTabOrder(this->m_names, this->m_pack);
		this->setTabOrder(this->m_pack, cancel);
		this->RefreshOkButton();
	}

	bool PackerDialog::IsNewFilename() const
	{
		const int currentIndex = this->m_names->currentRow() + 1;
		const int count = this->m_names->count();
		return currentIndex == count;
	}

	QString PackerDialog::SelectedMod() const
	{
		return this->m_mods->currentText();
	}

	QString PackerDialog::SelectedPluginItem() const
	{
		const QListWidgetItem* const currentItem = this->m_names->currentItem();
		// the item's own text: the new-name item shows words, not "<new filename>"
		return currentItem != nullptr ? currentItem->data(Qt::UserRole).toString() : QString();
	}

	void PackerDialog::RefreshModList()
	{
		const QStringList validMods = this->m_ModContext->GetValidMods();
		this->m_mods->clear();
		this->m_mods->addItems(validMods);
		this->m_mods->setEnabled(!validMods.isEmpty());
		if (validMods.isEmpty()) {
			this->m_note->setText(QObject::tr("No mod can be packed: the list has only "
			                                  "separators, the game's own DLC and Creation "
			                                  "Club content, backups, or mods MO2 can't read."));
			this->m_note->setVisible(true);
			this->m_names->setEnabled(false);
			return;
		}
		this->m_mods->setFocus();
	}

	void PackerDialog::UpdateNameList(const QString& modName)
	{
		this->m_names->clear();
		if (modName.isEmpty()) {
			this->RefreshOkButton();
			return;
		}
		const QString& modPath = this->m_ModContext->GetAbsoluteModPath(modName);
		const QStringList& filenames = this->m_ModContext->GetPlugins(modPath);
		for (const QString& name : filenames) {
			auto* item = new QListWidgetItem(this->m_names);
			item->setData(Qt::UserRole, name);
			item->setText(isNewNameItem(name) ? QObject::tr("Another name…") : name);
			if (isNewNameItem(name)) {
				item->setToolTip(QObject::tr("MO2 asks for the archive's name after Pack"));
			}
		}
		// the first plugin's name to begin with; 2.5.2 started with none chosen
		if (this->m_names->count() > 0) {
			this->m_names->setCurrentRow(0);
		}
		// a mod with no plugin: the archive needs one of its own name to load
		const bool noPlugin = this->m_names->count() <= 1;
		this->m_note->setObjectName(QStringLiteral("fluentSettingDescription"));
		this->m_note->style()->unpolish(this->m_note);
		this->m_note->style()->polish(this->m_note);
		// what the setting says in Settings, Plugins (it isn't translated)
		const bool dummyOn = this->m_SettingsService != nullptr &&
			this->m_SettingsService->GetPluginSetting(SettingsService::SETTING_CREATE_PLUGINS).toBool();
		const QString dummySetting =
			QStringLiteral("“Create a dummy plugin to load the archive if one does not exist.”");
		this->m_note->setText(!noPlugin ? QString()
			: dummyOn
			? QObject::tr("This mod has no plugin. As BSA Packer's settings ask, it also creates an "
			              "empty plugin with the archive's name, which the game needs to load the "
			              "archive.")
			: QObject::tr("This mod has no plugin, and the game loads an archive only with a "
			              "plugin of the same name. Add one, or turn on %1 in Settings, Plugins, "
			              "BSA Packer.").arg(dummySetting));
		this->m_note->setVisible(noPlugin);
		this->RefreshOkButton();
	}

	int PackerDialog::Exec()
	{
	 	return QDialog::exec();
	}

	void PackerDialog::RefreshSelectedName()
	{
		this->UpdateNameList(this->m_mods->currentText());
	}

	void PackerDialog::RefreshOkButton()
	{
		this->m_pack->setEnabled(this->m_names->currentItem() != nullptr &&
		                         !this->m_names->selectedItems().isEmpty() &&
		                         !this->m_mods->currentText().isEmpty());
	}
} // namespace BsaPacker
