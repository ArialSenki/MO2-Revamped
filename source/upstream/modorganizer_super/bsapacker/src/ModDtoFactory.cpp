#include <bsapacker/ModDtoFactory.h>

#include "ModDto.h"
#include "NullModDto.h"
#include "PackerDialog.h"
#include "fluentcontentdialog.h"
#include <QApplication>
#include <QLineEdit>

namespace BsaPacker
{
	const uint16_t FALLOUT_4_NEXUS_ID = 1151;
	const uint16_t STARFIELD_NEXUS_ID = 4187;

	ModDtoFactory::ModDtoFactory(
		const IModContext* modContext,
		IPackerDialog* packerDialog) :
		m_ModContext(modContext),
		m_PackerDialog(packerDialog)
	{
	}

	std::unique_ptr<IModDto> ModDtoFactory::Create() const
	{
		this->m_PackerDialog->RefreshModList();
		int result = m_PackerDialog->Exec();
		if (result != QDialog::DialogCode::Accepted)
		{
			return std::make_unique<NullModDto>();
		}

		const int nexusId = this->m_ModContext->GetNexusId();
		const QString& modName = this->m_PackerDialog->SelectedMod();
		const QString& modDir = this->m_ModContext->GetAbsoluteModPath(modName);
		const QString& pluginName = this->m_PackerDialog->SelectedPluginItem();
		const bool needsNewName = this->m_PackerDialog->IsNewFilename();
		const QString& archiveName = ModDtoFactory::ArchiveNameValidator(modName, pluginName, needsNewName);
		if (archiveName == nullptr) {
			return std::make_unique<NullModDto>();
		}

		const QString& archiveExtension = (nexusId == FALLOUT_4_NEXUS_ID || nexusId == STARFIELD_NEXUS_ID)
				? QStringLiteral(".ba2")
				: QStringLiteral(".bsa");

		return std::make_unique<ModDto>(nexusId, modDir, archiveName, archiveExtension);
	}

	QString ModDtoFactory::ArchiveNameValidator(
		const QString& modName,
		const QString& pluginName,
		const bool needsNewName)
	{
		QString archive_name_base;
		if (needsNewName) {
			// MO2 Revamped: a content dialog over MO2's window, in place of
			// QInputDialog with no parent; Pack waits for a name
			RevampedFluent::ContentDialog dialog(
				QApplication::activeWindow(), QStringLiteral("BsaPackerNameDialog"),
				QObject::tr("BSA Packer"));
			dialog.setHeading(QObject::tr("Name the archive"));
			auto* edit = new QLineEdit(modName, &dialog);
			dialog.body()->addWidget(RevampedFluent::fieldLabel(
				QObject::tr("Archive name (no file extension):"), edit, &dialog));
			dialog.body()->addWidget(edit);
			auto* pack = dialog.addButton(QObject::tr("Pack"), true);
			auto* cancel = dialog.addButton(QObject::tr("Cancel"));
			QObject::connect(pack, &QPushButton::clicked, &dialog, &QDialog::accept);
			QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
			QObject::connect(edit, &QLineEdit::textChanged, pack, [pack](const QString& text) {
				pack->setEnabled(!text.simplified().isEmpty());
			});
			pack->setEnabled(!modName.simplified().isEmpty());
			edit->selectAll();
			edit->setFocus();
			if (dialog.exec() != QDialog::Accepted) {
				return nullptr;
			}
			const QString name = edit->text().simplified();
			// Nameless plugins are not loaded in the games I tested. Nameless archives can be loaded in
			// some games using the SArchiveList INI setting or equivalent. It is simpler to just prevent nameless archives
			if (name.isEmpty()) {
				qWarning("Archive name cannot be empty. Cancelling archive creation.");
				return nullptr;
			}
			archive_name_base = name;
		} else {
			archive_name_base = pluginName.chopped(4); // trims the file extension off
		}
		return archive_name_base;
	}

	bool ModDtoFactory::CanOverwriteFile(const QString& filePath,
										 const QString& fileName)
	{
		const QString& absoluteFileName = filePath + '/' + fileName;
		const QFileInfo fileInfo(absoluteFileName);
		if (!fileInfo.exists() || !fileInfo.isFile()) {
			return true;
		}

		// MO2 Revamped: a question over MO2's window that starts on Cancel
		RevampedFluent::ContentDialog dialog(
			QApplication::activeWindow(), QStringLiteral("BsaPackerOverwriteDialog"),
			QObject::tr("BSA Packer"),
			QObject::tr("\"%1\" already exists. Overwrite it?").arg(absoluteFileName));
		dialog.setHeading(QObject::tr("Replace the archive?"));
		auto* overwrite = dialog.addButton(QObject::tr("Overwrite"), true);
		auto* cancel = dialog.addButton(QObject::tr("Cancel"));
		QObject::connect(overwrite, &QPushButton::clicked, &dialog, &QDialog::accept);
		QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
		dialog.askFirst(cancel);
		return dialog.exec() == QDialog::Accepted;
	}
} // namespace BsaPacker
