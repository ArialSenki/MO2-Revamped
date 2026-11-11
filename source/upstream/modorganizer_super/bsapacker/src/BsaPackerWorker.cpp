#include "BsaPackerWorker.h"

#include <QApplication>
#include <QMessageBox>

#include "fluentcontentdialog.h"

#include <bsapacker/ArchiveBuildDirector.h>
#include <bsapacker/ModDtoFactory.h>

namespace BsaPacker
{
	BsaPackerWorker::BsaPackerWorker(
		const ISettingsService* settingsService,
		const IModDtoFactory* modDtoFactory,
		const IArchiveBuilderFactory* archiveBuilderFactory,
		const IArchiveAutoService* archiveAutoService,
		const IDummyPluginServiceFactory* dummyPluginServiceFactory,
		const IHideLooseAssetService* hideLooseAssetService,
		const IArchiveNameService* archiveNameService,
		const IOverrideFileService* overrideFileService) :
		m_SettingsService(settingsService),
		m_ModDtoFactory(modDtoFactory),
		m_ArchiveBuilderFactory(archiveBuilderFactory),
		m_ArchiveAutoService(archiveAutoService),
		m_DummyPluginServiceFactory(dummyPluginServiceFactory),
		m_HideLooseAssetService(hideLooseAssetService),
		m_ArchiveNameService(archiveNameService),
		m_OverrideFileService(overrideFileService)
	{
	}

	void BsaPackerWorker::DoWork() const
	{
		QStringList createdArchives;
		const std::unique_ptr<IModDto> modDto = this->m_ModDtoFactory->Create(); // handles PackerDialog and validation, implements Null Object pattern
		const std::vector<bsa_archive_type_e> types = this->m_ArchiveBuilderFactory->GetArchiveTypes(modDto.get());
		for (auto&& type : types) {
			const std::unique_ptr<IArchiveBuilder> builder = this->m_ArchiveBuilderFactory->Create(type, modDto.get());
			ArchiveBuildDirector director(this->m_SettingsService, builder.get());
			director.Construct(); // must check if cancelled
			const std::vector<std::unique_ptr<libbsarch::bs_archive_auto>> archives = builder->getArchives();
			for (const auto& archive : archives) {
				if (archive) {
					const QFileInfo fileInfo(this->m_ArchiveNameService->GetArchiveFullPath(type, modDto.get()));
					bool res = this->m_ArchiveAutoService->CreateBSA(archive.get(), fileInfo.absoluteFilePath(), type);
					if (res) {
						// MO2 Revamped: from MO2 a7dad4a
						createdArchives.append(fileInfo.completeBaseName());
					}
				}
			}
		}

		// MO2 Revamped: the plugin, the hiding and the summary only follow an
		// archive that was made; 2.5.2 hid the mod's loose files even when none was
		if (modDto->Directory().isEmpty()) {
			return;  // the dialog was cancelled
		}
		if (createdArchives.isEmpty()) {
			RevampedFluent::showNotice(QApplication::activeWindow(), QObject::tr("BSA Packer"),
				QObject::tr("No archive was created: the mod has no files to pack, or packing "
				            "was cancelled. Its files are as they were."));
			return;
		}
		this->m_OverrideFileService->CreateOverrideFile(modDto->NexusId(), modDto->Directory(), createdArchives);

		const std::unique_ptr<IDummyPluginService> pluginService = this->m_DummyPluginServiceFactory->Create();
		pluginService->CreatePlugin(modDto->Directory(), modDto->ArchiveName());

		this->m_HideLooseAssetService->HideLooseAssets(modDto->Directory());

		QStringList names;
		for (const QString& name : createdArchives) {
			names.append(name + modDto->ArchiveExtension());
		}
		RevampedFluent::showNotice(QApplication::activeWindow(), QObject::tr("BSA Packer"),
			QObject::tr("Created:") + "\n" + names.join("\n"));
	}
}
