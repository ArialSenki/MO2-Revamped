#include "settingsdialogpaths.h"
#include "fluenticons.h"
#include "circularuihelpers.h"
#include "shared/appconfig.h"
#include "ui_settingsdialog.h"
#include <iplugingame.h>

#include <QDir>
#include <QGridLayout>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>
#include <tuple>
#include <vector>

namespace
{
// the fields show Windows paths, like the game's executable below them; the
// settings keep the forward slashes they always had
QString shown(const QString& path)
{
  return QDir::toNativeSeparators(path);
}

QString stored(const QLineEdit* edit)
{
  // a pasted " D:\MO2" would otherwise be saved as a relative path
  return QDir::fromNativeSeparators(edit->text().trimmed());
}

// folders compare the way Windows sees them: case and separators don't matter
QString comparable(const QString& path)
{
  return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

bool samePath(const QString& a, const QString& b)
{
  return comparable(a) == comparable(b);
}

// true if 'path' is 'folder' or somewhere inside it
bool isInside(const QString& path, const QString& folder)
{
  const QString p = comparable(path);
  QString f       = comparable(folder);
  if (p == f) {
    return true;
  }
  if (!f.endsWith(QLatin1Char('/'))) {
    f += QLatin1Char('/');
  }
  return p.startsWith(f);
}

// a drive or network path; anything else would resolve against MO2's working
// directory
bool isFullPath(const QString& path)
{
  const QString p = QDir::fromNativeSeparators(path);
  return (p.size() >= 3 && p[1] == QLatin1Char(':') && p[2] == QLatin1Char('/')) ||
         p.startsWith(QLatin1String("//"));
}

// the folder a storage field stands for once %BASE_DIR% is expanded; an empty
// field is the default folder in the base
QString effectivePath(const QString& value, const QString& base,
                      const std::wstring& defaultName)
{
  if (value.isEmpty()) {
    return base + "/" + QString::fromStdWString(defaultName);
  }
  return PathSettings::resolve(value, base);
}

// the base being saved; an empty field is the instance's own folder
QString typedBase(const QLineEdit* edit)
{
  const QString base = stored(edit);
  return base.isEmpty()
             ? QDir::fromNativeSeparators(qApp->property("dataPath").toString())
             : base;
}
}  // namespace

namespace
{

void addPathRow(QGridLayout* layout, int row, QLabel* label, QLineEdit* edit,
                QPushButton* browseButton)
{
  label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  label->setMinimumWidth(128);
  browseButton->setText(QObject::tr("Browse"));
  // six buttons named the same: screen readers hear which folder each is for
  browseButton->setAccessibleName(QObject::tr("Browse") + QStringLiteral(": ") +
                                  QString(label->text()).remove(QLatin1Char('&')));
  browseButton->setMinimumWidth(108);
  browseButton->setMinimumHeight(34);
  browseButton->setIcon(FluentIcons::glyph(0xE8DA));
  browseButton->setIconSize(QSize(16, 16));

  layout->addWidget(label, row, 0);
  layout->addWidget(edit, row, 1);
  layout->addWidget(browseButton, row, 2);
}

}  // namespace

PathsSettingsTab::PathsSettingsTab(Settings& s, SettingsDialog& d)
    : SettingsTab(s, d), m_gameDir(settings().game().plugin()->gameDirectory())
{
  MOBase::CircularUi::addIconToHeading(
      ui->pathsPageTitle, ":/MO/gui/mainwindow/files/folder.svg",
      QStringLiteral("pathsPageIcon"), 36, QStringLiteral("P"));

  // Reuse the existing path controls and signal handlers, but group them into
  // the same compact cards used by the other redesigned Settings pages.
  QVBoxLayout* pageLayout = qobject_cast<QVBoxLayout*>(ui->pathsTab->layout());
  if (pageLayout) {
    pageLayout->setContentsMargins(8, 8, 8, 8);
    pageLayout->setSpacing(10);
    pageLayout->removeItem(ui->gridLayout_2);
    delete ui->gridLayout_2;
    pageLayout->removeWidget(ui->label_23);

    const int spacerIndex = pageLayout->indexOf(ui->verticalSpacer_6);
    if (spacerIndex >= 0) {
      delete pageLayout->takeAt(spacerIndex);
    }

    auto* storageGroup = new QGroupBox(QObject::tr("Instance storage"),
                                       ui->pathsTab);
    storageGroup->setObjectName(QStringLiteral("pathsStorageGroup"));
    auto* storageLayout = new QGridLayout(storageGroup);
    storageLayout->setContentsMargins(10, 10, 10, 10);
    storageLayout->setHorizontalSpacing(12);
    storageLayout->setVerticalSpacing(8);

    addPathRow(storageLayout, 0, ui->label_25, ui->baseDirEdit,
               ui->browseBaseDirBtn);
    addPathRow(storageLayout, 1, ui->label_7, ui->downloadDirEdit,
               ui->browseDownloadDirBtn);
    addPathRow(storageLayout, 2, ui->label_8, ui->modDirEdit,
               ui->browseModDirBtn);
    addPathRow(storageLayout, 3, ui->label_9, ui->cacheDirEdit,
               ui->browseCacheDirBtn);
    addPathRow(storageLayout, 4, ui->label_22, ui->profilesDirEdit,
               ui->browseProfilesDirBtn);
    addPathRow(storageLayout, 5, ui->label_24, ui->overwriteDirEdit,
               ui->browseOverwriteDirBtn);
    ui->label_26->setObjectName(QStringLiteral("pathsBaseDirHint"));
    ui->label_26->setWordWrap(true);
    ui->label_26->setContentsMargins(4, 0, 4, 0);
    storageLayout->addWidget(ui->label_26, 6, 0, 1, 3);
    ui->label_23->setObjectName(QStringLiteral("pathsWritableHint"));
    ui->label_23->setText(QObject::tr("All directories must be writable."));
    ui->label_23->setContentsMargins(4, 0, 4, 0);
    storageLayout->addWidget(ui->label_23, 7, 0, 1, 3);
    storageLayout->setColumnStretch(1, 1);
    MOBase::CircularUi::addGroupHeading(
        storageGroup, ":/MO/gui/mainwindow/files/folder.svg",
        QStringLiteral("pathsStorageIcon"), 26,
        QStringLiteral("pathsSectionTitle"), QStringLiteral("S"));

    auto* gameGroup = new QGroupBox(QObject::tr("Managed game"), ui->pathsTab);
    gameGroup->setObjectName(QStringLiteral("pathsGameGroup"));
    auto* gameLayout = new QGridLayout(gameGroup);
    gameLayout->setContentsMargins(10, 10, 10, 10);
    gameLayout->setHorizontalSpacing(12);
    gameLayout->setVerticalSpacing(8);
    ui->label_29->setText(QObject::tr("Executable"));
    addPathRow(gameLayout, 0, ui->label_29, ui->managedGameDirEdit,
               ui->browseGameDirBtn);
    gameLayout->setColumnStretch(1, 1);
    MOBase::CircularUi::addGroupHeading(
        gameGroup, ":/MO/gui/mainwindow/files/executable.svg",
        QStringLiteral("pathsGameIcon"), 26,
        QStringLiteral("pathsSectionTitle"), QStringLiteral("G"));

    pageLayout->addWidget(storageGroup);
    pageLayout->addWidget(gameGroup);
    pageLayout->addStretch(1);
  }

  ui->baseDirEdit->setText(shown(settings().paths().base()));

  ui->managedGameDirEdit->setText(QDir::toNativeSeparators(
      m_gameDir.absoluteFilePath(settings().game().plugin()->binaryName())));

  QString basePath = settings().paths().base();
  QDir baseDir(basePath);

  for (const auto& dir :
       {std::make_pair(ui->downloadDirEdit, settings().paths().downloads(false)),
        std::make_pair(ui->modDirEdit, settings().paths().mods(false)),
        std::make_pair(ui->cacheDirEdit, settings().paths().cache(false)),
        std::make_pair(ui->profilesDirEdit, settings().paths().profiles(false)),
        std::make_pair(ui->overwriteDirEdit, settings().paths().overwrite(false))}) {

    QString storePath = baseDir.relativeFilePath(dir.second);
    storePath         = dir.second;
    dir.first->setText(shown(storePath));
  }

  QObject::connect(ui->browseBaseDirBtn, &QPushButton::clicked, [&] {
    on_browseBaseDirBtn_clicked();
  });
  QObject::connect(ui->browseCacheDirBtn, &QPushButton::clicked, [&] {
    on_browseCacheDirBtn_clicked();
  });
  QObject::connect(ui->browseDownloadDirBtn, &QPushButton::clicked, [&] {
    on_browseDownloadDirBtn_clicked();
  });
  QObject::connect(ui->browseGameDirBtn, &QPushButton::clicked, [&] {
    on_browseGameDirBtn_clicked();
  });
  QObject::connect(ui->browseModDirBtn, &QPushButton::clicked, [&] {
    on_browseModDirBtn_clicked();
  });
  QObject::connect(ui->browseOverwriteDirBtn, &QPushButton::clicked, [&] {
    on_browseOverwriteDirBtn_clicked();
  });
  QObject::connect(ui->browseProfilesDirBtn, &QPushButton::clicked, [&] {
    on_browseProfilesDirBtn_clicked();
  });

  QObject::connect(ui->baseDirEdit, &QLineEdit::editingFinished, [&] {
    on_baseDirEdit_editingFinished();
  });
  QObject::connect(ui->cacheDirEdit, &QLineEdit::editingFinished, [&] {
    on_cacheDirEdit_editingFinished();
  });
  QObject::connect(ui->downloadDirEdit, &QLineEdit::editingFinished, [&] {
    on_downloadDirEdit_editingFinished();
  });
  QObject::connect(ui->modDirEdit, &QLineEdit::editingFinished, [&] {
    on_modDirEdit_editingFinished();
  });
  QObject::connect(ui->overwriteDirEdit, &QLineEdit::editingFinished, [&] {
    on_overwriteDirEdit_editingFinished();
  });
  QObject::connect(ui->profilesDirEdit, &QLineEdit::editingFinished, [&] {
    on_profilesDirEdit_editingFinished();
  });
}

bool PathsSettingsTab::validate()
{
  const QString basePath = typedBase(ui->baseDirEdit);
  const bool baseChanged = !samePath(basePath, settings().paths().base());

  if (baseChanged && !isFullPath(basePath)) {
    QMessageBox::warning(parentWidget(), QObject::tr("Error"),
                         QObject::tr("\"%1\" isn't a full path. Use a folder that "
                                     "starts with a drive letter, such as D:\\MO2.")
                             .arg(QDir::toNativeSeparators(basePath)));
    return false;
  }

  struct Folder
  {
    const QLabel* label;
    QString path;
    bool changed;
  };

  using Getter = QString (PathSettings::*)(bool) const;
  std::vector<Folder> folders;
  bool anyChanged = baseChanged;

  for (const auto& [label, edit, getter, defaultName] :
       {std::tuple<QLabel*, QLineEdit*, Getter, std::wstring>{
            ui->label_7, ui->downloadDirEdit, &PathSettings::downloads,
            AppConfig::downloadPath()},
        std::tuple<QLabel*, QLineEdit*, Getter, std::wstring>{
            ui->label_8, ui->modDirEdit, &PathSettings::mods, AppConfig::modsPath()},
        std::tuple<QLabel*, QLineEdit*, Getter, std::wstring>{
            ui->label_9, ui->cacheDirEdit, &PathSettings::cache, AppConfig::cachePath()},
        std::tuple<QLabel*, QLineEdit*, Getter, std::wstring>{
            ui->label_22, ui->profilesDirEdit, &PathSettings::profiles,
            AppConfig::profilesPath()},
        std::tuple<QLabel*, QLineEdit*, Getter, std::wstring>{
            ui->label_24, ui->overwriteDirEdit, &PathSettings::overwrite,
            AppConfig::overwritePath()}}) {
    const QString path = effectivePath(stored(edit), basePath, defaultName);
    // folders left as they were aren't checked, so a stored path on a drive
    // that is offline doesn't block saving other settings
    const bool changed = baseChanged || !samePath(path, (settings().paths().*getter)(true));

    // a folder under %BASE_DIR% follows the base folder, which may be stored
    // relative from before; that base wasn't changed here, so it isn't judged
    const bool underUnchangedBase =
        !baseChanged &&
        stored(edit).trimmed().startsWith(QLatin1String("%BASE_DIR%"), Qt::CaseInsensitive);
    if (changed && !underUnchangedBase && !isFullPath(path)) {
      QMessageBox::warning(parentWidget(), QObject::tr("Error"),
                           QObject::tr("\"%1\" isn't a full path. Use a folder that "
                                       "starts with a drive letter or with %BASE_DIR%.")
                               .arg(QDir::toNativeSeparators(path)));
      return false;
    }

    anyChanged = anyChanged || changed;
    folders.push_back({label, path, changed});
  }

  if (!anyChanged) {
    return true;
  }

  // MO2 would mix their files: overwrite inside mods would show up as a mod
  for (std::size_t i = 0; i < folders.size(); ++i) {
    for (std::size_t j = i + 1; j < folders.size(); ++j) {
      // a layout that was already there isn't refused for a change elsewhere
      if (!folders[i].changed && !folders[j].changed) {
        continue;
      }
      if (isInside(folders[i].path, folders[j].path) ||
          isInside(folders[j].path, folders[i].path)) {
        QMessageBox::warning(
            parentWidget(), QObject::tr("Error"),
            QObject::tr("These folders can't be the same or one inside the other:\n"
                        "%1 %2\n%3 %4")
                .arg(QString(folders[i].label->text()).remove(QLatin1Char('&')),
                     QDir::toNativeSeparators(folders[i].path),
                     QString(folders[j].label->text()).remove(QLatin1Char('&')),
                     QDir::toNativeSeparators(folders[j].path)));
        return false;
      }
    }
  }

  return true;
}

bool PathsSettingsTab::baseChanged()
{
  return !samePath(typedBase(ui->baseDirEdit), settings().paths().base());
}

void PathsSettingsTab::update()
{
  using Setter    = void (PathSettings::*)(const QString&);
  using Getter    = QString (PathSettings::*)(bool) const;
  using Directory = std::tuple<QString, Setter, Getter, std::wstring>;

  // the base being saved: a folder typed to stay where it is, outside the new
  // base, keeps its path instead of following the base
  const QString basePath = typedBase(ui->baseDirEdit);
  const bool baseMoved   = !samePath(basePath, settings().paths().base());

  for (const Directory& dir :
       {Directory{stored(ui->downloadDirEdit), &PathSettings::setDownloads,
                  &PathSettings::downloads, AppConfig::downloadPath()},
        Directory{stored(ui->cacheDirEdit), &PathSettings::setCache,
                  &PathSettings::cache, AppConfig::cachePath()},
        Directory{stored(ui->modDirEdit), &PathSettings::setMods, &PathSettings::mods,
                  AppConfig::modsPath()},
        Directory{stored(ui->overwriteDirEdit), &PathSettings::setOverwrite,
                  &PathSettings::overwrite, AppConfig::overwritePath()},
        Directory{stored(ui->profilesDirEdit), &PathSettings::setProfiles,
                  &PathSettings::profiles, AppConfig::profilesPath()}}) {
    QString path;
    Setter setter;
    Getter getter;
    std::wstring defaultName;
    std::tie(path, setter, getter, defaultName) = dir;

    QString realPath = path;
    realPath         = PathSettings::resolve(realPath, basePath);

    // only a folder that moved is created: an unchanged one on a drive that
    // is offline right now stays as it is
    const bool moved =
        baseMoved ||
        !samePath(effectivePath(path, basePath, defaultName),
                  (settings().paths().*getter)(true));

    if (moved && !realPath.isEmpty() && !QDir(realPath).exists()) {
      if (!QDir().mkpath(realPath)) {
        QMessageBox::warning(
            parentWidget(), QObject::tr("Error"),
            QObject::tr("The folder \"%1\" couldn't be created (check the drive and "
                        "your permissions). This path wasn't changed.")
                .arg(realPath));

        continue;
      }
    }

    if (QFileInfo(realPath) !=
        QFileInfo(basePath + "/" + QString::fromStdWString(defaultName))) {
      (settings().paths().*setter)(path);
    } else {
      (settings().paths().*setter)("");
    }
  }

  if (QFileInfo(stored(ui->baseDirEdit)) !=
      QFileInfo(qApp->property("dataPath").toString())) {
    settings().paths().setBase(stored(ui->baseDirEdit));
  } else {
    settings().paths().setBase("");
  }

  if (m_gameDir != settings().game().plugin()->gameDirectory()) {
    settings().game().setDirectory(m_gameDir.absolutePath());
  }
}

void PathsSettingsTab::on_browseBaseDirBtn_clicked()
{
  QString temp = QFileDialog::getExistingDirectory(
      &dialog(), QObject::tr("Select base directory"), stored(ui->baseDirEdit));
  if (!temp.isEmpty()) {
    ui->baseDirEdit->setText(shown(temp));
  }
}

void PathsSettingsTab::on_browseDownloadDirBtn_clicked()
{
  QString searchPath = stored(ui->downloadDirEdit);
  searchPath         = PathSettings::resolve(searchPath, stored(ui->baseDirEdit));

  QString temp = QFileDialog::getExistingDirectory(
      &dialog(), QObject::tr("Select download directory"), searchPath);
  if (!temp.isEmpty()) {
    ui->downloadDirEdit->setText(shown(temp));
  }
}

void PathsSettingsTab::on_browseModDirBtn_clicked()
{
  QString searchPath = stored(ui->modDirEdit);
  searchPath         = PathSettings::resolve(searchPath, stored(ui->baseDirEdit));

  QString temp = QFileDialog::getExistingDirectory(
      &dialog(), QObject::tr("Select mod directory"), searchPath);
  if (!temp.isEmpty()) {
    ui->modDirEdit->setText(shown(temp));
  }
}

void PathsSettingsTab::on_browseCacheDirBtn_clicked()
{
  QString searchPath = stored(ui->cacheDirEdit);
  searchPath         = PathSettings::resolve(searchPath, stored(ui->baseDirEdit));

  QString temp = QFileDialog::getExistingDirectory(
      &dialog(), QObject::tr("Select cache directory"), searchPath);
  if (!temp.isEmpty()) {
    ui->cacheDirEdit->setText(shown(temp));
  }
}

void PathsSettingsTab::on_browseProfilesDirBtn_clicked()
{
  QString searchPath = stored(ui->profilesDirEdit);
  searchPath         = PathSettings::resolve(searchPath, stored(ui->baseDirEdit));

  QString temp = QFileDialog::getExistingDirectory(
      &dialog(), QObject::tr("Select profiles directory"), searchPath);
  if (!temp.isEmpty()) {
    ui->profilesDirEdit->setText(shown(temp));
  }
}

void PathsSettingsTab::on_browseOverwriteDirBtn_clicked()
{
  QString searchPath = stored(ui->overwriteDirEdit);
  searchPath         = PathSettings::resolve(searchPath, stored(ui->baseDirEdit));

  QString temp = QFileDialog::getExistingDirectory(
      &dialog(), QObject::tr("Select overwrite directory"), searchPath);
  if (!temp.isEmpty()) {
    ui->overwriteDirEdit->setText(shown(temp));
  }
}

void PathsSettingsTab::on_browseGameDirBtn_clicked()
{
  QFileInfo oldGameExe(ui->managedGameDirEdit->text());

  // this gets the name of the game executable
  //
  // spaces in the name are interpreted as separators ";" in the filter,
  // so we replace them with the single character matching wildcard "?"
  //
  QString temp = QFileDialog::getOpenFileName(
      &dialog(), QObject::tr("Select game executable"), oldGameExe.absolutePath(),
      oldGameExe.fileName().replace(QRegularExpression(" "), "?"));

  if (temp.isEmpty()) {
    return;
  }

  // we need to find the game folder corresponding to the executable
  //
  // some game plugins have executable in subfolder, e.g. bin/game.exe,
  // so we need to go up the parent folders until the concatenation of
  // the folder and the binary path equals the game executable specified
  // by the user
  //
  QFileInfo newExe(temp);
  const auto binaryPath = settings().game().plugin()->binaryName();
  QDir folder           = newExe.absoluteDir();
  while (folder.exists() && !folder.isRoot() &&
         QFileInfo(folder.filePath(binaryPath)) != newExe) {
    folder.cdUp();
  }

  if (folder.exists(binaryPath)) {
    m_gameDir = folder;
    ui->managedGameDirEdit->setText(QDir::toNativeSeparators(
        m_gameDir.absoluteFilePath(settings().game().plugin()->binaryName())));
  } else {
    QMessageBox::warning(
        parentWidget(), QObject::tr("Error"),
        QObject::tr("The given path was not recognized as a valid game installation. "
                    "The current game plugin requires the executable to be in a \"%1\" "
                    "subfolder of the game directory.")
            .arg(QFileInfo(binaryPath).path()));
  }
}

void PathsSettingsTab::on_baseDirEdit_editingFinished()
{
  normalizePath(ui->baseDirEdit);
}

void PathsSettingsTab::on_downloadDirEdit_editingFinished()
{
  normalizePath(ui->downloadDirEdit);
}

void PathsSettingsTab::on_modDirEdit_editingFinished()
{
  normalizePath(ui->modDirEdit);
}

void PathsSettingsTab::on_cacheDirEdit_editingFinished()
{
  normalizePath(ui->cacheDirEdit);
}

void PathsSettingsTab::on_profilesDirEdit_editingFinished()
{
  normalizePath(ui->profilesDirEdit);
}

void PathsSettingsTab::on_overwriteDirEdit_editingFinished()
{
  normalizePath(ui->overwriteDirEdit);
}

void PathsSettingsTab::normalizePath(QLineEdit* lineEdit)
{
  QString text = lineEdit->text().trimmed();
  while (text.endsWith('/') || text.endsWith('\\')) {
    text.chop(1);
  }
  lineEdit->setText(shown(text));
}
