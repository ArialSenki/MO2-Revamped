#include "instancemanagerdialog.h"
#include "createinstancedialog.h"
#include "filesystemutilities.h"
#include "instancemanager.h"
#include "plugincontainer.h"
#include "selectiondialog.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "shared/util.h"
#include "ui_instancemanagerdialog.h"
#include <iplugingame.h>
#include <report.h>
#include <utility.h>

#include <QFileInfo>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QVBoxLayout>

using namespace MOBase;

// returns the icon for the given instance or an empty 32x32 icon if the game
// plugin couldn't be found
//
QIcon instanceIcon(PluginContainer& pc, const Instance& i)
{
  auto* game = InstanceManager::singleton().gamePluginForDirectory(i.directory(), pc);

  if (!game) {
    QPixmap empty(32, 32);
    empty.fill(QColor(0, 0, 0, 0));
    return QIcon(empty);
  }

  // it's possible to have the game installed in a way that the game plugin
  // couldn't auto detect; in this case, the instance would have a valid game
  // directory, but the plugin wouldn't know about it
  //
  // it's also possible, but unlikely, to have multiple installations of the
  // same game that have different icons for the same exe
  //
  // so the game directory specified for the instance needs to be given to the
  // game plugin to get the appropriate icon, but since these game plugin
  // objects are created on startup and are global, they should retain their
  // auto detected path
  //
  // if not, creating a new instance for a specific plugin would use the game
  // directory of the instance for which the icon was most recently shown, which
  // would be really inconsistent
  //
  //
  // this game plugin could also be the currently active plugin for the
  // current instance, which should _definitely_ keep pointing to the same
  // directory as before

  // remember old game directory
  //
  // note that gameDirectory() returns a QDir, which doesn't support empty
  // strings (they get converted to "." automatically!), but the plugin _will_
  // try to return an empty string when the game has not been auto-detected
  //
  // so gameDirectory() _cannot_ reliably be used if `isInstalled()` is false
  const QString old = game->isInstalled() ? game->gameDirectory().path() : "";

  // revert
  Guard g([&] {
    game->setGamePath(old);
  });

  // set directory for this instance
  game->setGamePath(i.gameDirectory());

  return game->gameIcon();
}

// pops up a dialog to ask for an instance name when renaming
//
QString getInstanceName(QWidget* parent, const QString& title, const QString& moreText,
                        const QString& label, const QString& parentDirectory,
                        const QString& oldName = {})
{
  QDialog dlg(parent);
  dlg.setObjectName(QStringLiteral("InstanceNameDialog"));
  dlg.setWindowTitle(title);
  dlg.setMinimumWidth(460);

  auto* ly = new QVBoxLayout(&dlg);
  ly->setContentsMargins(22, 18, 22, 18);
  ly->setSpacing(14);

  auto* header = new QWidget(&dlg);
  header->setObjectName(QStringLiteral("instanceNameHeader"));
  auto* headerLayout = new QVBoxLayout(header);
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(4);

  auto* heading = new QLabel(title, header);
  heading->setObjectName(QStringLiteral("instanceNameHeading"));
  auto* description = new QLabel(
      moreText.isEmpty()
          ? QObject::tr("Choose a clear folder name for this instance.")
          : moreText,
      header);
  description->setObjectName(QStringLiteral("instanceNameDescription"));
  description->setWordWrap(true);
  headerLayout->addWidget(heading);
  headerLayout->addWidget(description);
  ly->addWidget(header);

  auto* nameCard = new QGroupBox(QObject::tr("Instance name"), &dlg);
  nameCard->setObjectName(QStringLiteral("instanceNameCard"));
  auto* nameLayout = new QVBoxLayout(nameCard);
  nameLayout->setContentsMargins(12, 14, 12, 12);
  nameLayout->setSpacing(7);

  auto* bb = new QDialogButtonBox(
      QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dlg);
  bb->setObjectName(QStringLiteral("instanceNameButtons"));
  bb->button(QDialogButtonBox::Ok)->setText(QObject::tr("Rename"));

  auto* text = new QLineEdit(oldName, nameCard);
  text->setObjectName(QStringLiteral("instanceNameEdit"));
  text->setPlaceholderText(QObject::tr("Enter an instance name"));
  text->selectAll();

  auto* lb = new QLabel(label, nameCard);
  lb->setObjectName(QStringLiteral("instanceNameLabel"));
  lb->setWordWrap(true);
  auto* error = new QLabel(nameCard);
  error->setObjectName(QStringLiteral("instanceNameValidation"));
  error->setWordWrap(true);

  nameLayout->addWidget(lb);
  nameLayout->addWidget(text);
  nameLayout->addWidget(error);
  ly->addWidget(nameCard);
  ly->addStretch(1);
  ly->addWidget(bb);

  auto check = [&] {
    bool okay = false;

    if (text->text().isEmpty()) {
      error->setText("");
    } else if (!MOBase::validFileName(text->text())) {
      error->setText(QObject::tr("The instance name must be a valid folder name."));
    } else {
      const auto name = MOBase::sanitizeFileName(text->text());

      if ((name != oldName) && QDir(parentDirectory).exists(name)) {
        error->setText(QObject::tr("An instance with this name already exists."));
      } else {
        okay = true;
      }
    }

    error->setVisible(!error->text().isEmpty());
    bb->button(QDialogButtonBox::Ok)->setEnabled(okay);
  };

  QObject::connect(text, &QLineEdit::textChanged, [&] {
    check();
  });
  QObject::connect(bb, &QDialogButtonBox::accepted, [&] {
    dlg.accept();
  });
  QObject::connect(bb, &QDialogButtonBox::rejected, [&] {
    dlg.reject();
  });

  check();

  dlg.resize({520, 250});
  if (dlg.exec() != QDialog::Accepted) {
    return {};
  }

  return MOBase::sanitizeFileName(text->text());
}

void showInstanceExample(QWidget* parent)
{
  QDialog dlg(parent);
  dlg.setObjectName(QStringLiteral("InstanceHelpDialog"));
  dlg.setWindowTitle(QObject::tr("About instances"));
  dlg.setMinimumWidth(540);
  dlg.resize(580, 410);

  auto* layout = new QVBoxLayout(&dlg);
  layout->setContentsMargins(20, 18, 20, 18);
  layout->setSpacing(12);

  auto* header = new QFrame(&dlg);
  header->setObjectName(QStringLiteral("instanceHelpHeaderPanel"));
  auto* headerLayout = new QVBoxLayout(header);
  headerLayout->setContentsMargins(16, 14, 16, 14);
  headerLayout->setSpacing(5);

  auto* heading = new QLabel(QObject::tr("An instance is its own MO2 workspace."), header);
  heading->setObjectName(QStringLiteral("instanceHelpHeading"));
  auto* description = new QLabel(
      QObject::tr("Each instance keeps its profiles, mods, downloads and settings together. "
                  "Use separate instances when you want independent setups."),
      header);
  description->setObjectName(QStringLiteral("instanceHelpDescription"));
  description->setWordWrap(true);
  headerLayout->addWidget(heading);
  headerLayout->addWidget(description);
  layout->addWidget(header);

  auto addExample = [&](const QString& game, const QString& detail) {
    auto* card = new QFrame(&dlg);
    card->setObjectName(QStringLiteral("instanceHelpExampleCard"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 10, 14, 10);
    cardLayout->setSpacing(3);

    auto* gameLabel = new QLabel(game, card);
    gameLabel->setObjectName(QStringLiteral("instanceHelpGame"));
    auto* detailLabel = new QLabel(detail, card);
    detailLabel->setObjectName(QStringLiteral("instanceHelpExampleText"));
    detailLabel->setWordWrap(true);
    cardLayout->addWidget(gameLabel);
    cardLayout->addWidget(detailLabel);
    layout->addWidget(card);
  };

  addExample(QObject::tr("Elden Ring"),
             QObject::tr("Keep Elden Ring profiles, mods, downloads and settings in this instance."));
  addExample(QObject::tr("Skyrim Special Edition"),
             QObject::tr("Manage Skyrim separately, without mixing its setup with Elden Ring."));

  auto* footer = new QLabel(
      QObject::tr("Choose the instance you want to manage from this window."), &dlg);
  footer->setObjectName(QStringLiteral("instanceHelpFooter"));
  footer->setWordWrap(true);
  layout->addWidget(footer);
  layout->addStretch(1);

  auto* closeButton = new QPushButton(QObject::tr("Close"), &dlg);
  closeButton->setObjectName(QStringLiteral("instanceHelpCloseButton"));
  closeButton->setDefault(true);
  auto* buttonLayout = new QHBoxLayout;
  buttonLayout->addStretch(1);
  buttonLayout->addWidget(closeButton);
  layout->addLayout(buttonLayout);

  QObject::connect(closeButton, &QPushButton::clicked, &dlg, &QDialog::accept);
  dlg.exec();
}

InstanceManagerDialog::~InstanceManagerDialog() = default;

InstanceManagerDialog::InstanceManagerDialog(PluginContainer& pc, QWidget* parent)
    : QDialog(parent), ui(new Ui::InstanceManagerDialog), m_pc(pc), m_model(nullptr),
      m_restartOnSelect(true)
{
  ui->setupUi(this);

  ui->splitter->setSizes({250, 1});
  ui->splitter->setStretchFactor(0, 0);
  ui->splitter->setStretchFactor(1, 1);

  m_model = new QStandardItemModel;
  ui->list->setModel(m_model);

  m_filter.setEdit(ui->filter);
  m_filter.setList(ui->list);
  m_filter.setFilteredBorder(false);

  setMinimumSize(860, 560);
  ui->createNew->setMinimumHeight(34);
  ui->list->setIconSize(QSize(36, 36));
  ui->list->setSpacing(4);
  ui->list->setMinimumWidth(225);
  ui->horizontalLayout_2->setContentsMargins(12, 8, 12, 8);
  ui->horizontalLayout_2->setSpacing(12);
  ui->verticalLayout->setContentsMargins(12, 12, 12, 12);
  ui->verticalLayout->setSpacing(10);
  ui->verticalLayout_2->setContentsMargins(16, 14, 16, 16);
  ui->verticalLayout_2->setSpacing(10);
  ui->openINI->setToolTip(
      tr("Open this instance's ModOrganizer.ini with the associated editor."));
  ui->switchToInstance->setToolTip(
      tr("Restart Mod Organizer and open the selected instance."));
  ui->detailsHeading->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  ui->selectionHint->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  ui->instanceStatus->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  ui->instanceStatus->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  ui->widget_7->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  ui->widget_9->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  for (QLineEdit* field : ui->widget_7->findChildren<QLineEdit*>()) {
    field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }
  if (auto* detailsLayout = qobject_cast<QGridLayout*>(ui->widget_7->layout())) {
    detailsLayout->setContentsMargins(0, 8, 0, 8);
    detailsLayout->setHorizontalSpacing(12);
    detailsLayout->setVerticalSpacing(9);
    detailsLayout->setColumnMinimumWidth(0, 100);
    detailsLayout->setColumnStretch(1, 1);
  }
  for (QLabel* label : ui->widget_7->findChildren<QLabel*>()) {
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  }
  if (auto* actionLayout = qobject_cast<QHBoxLayout*>(ui->widget_9->layout())) {
    actionLayout->setSpacing(8);
  }

  if (InstanceManager::singleton().isEldenRingOnlyPortableMode()) {
    ui->createNew->setVisible(false);
  }

  updateInstances();
  updateList();
  ui->list->clearSelection();
  ui->list->setCurrentIndex(QModelIndex());
  clearData();

  connect(ui->createNew, &QPushButton::clicked, [&] {
    createNew();
  });
  ui->instanceHelpButton->setToolTip(
      tr("See a local example of how separate MO2 instances work."));
  connect(ui->instanceHelpButton, &QPushButton::clicked, this, [this] {
    showInstanceExample(this);
  });

  connect(ui->list->selectionModel(), &QItemSelectionModel::selectionChanged, [&] {
    onSelection();
  });
  connect(ui->list, &QListView::activated, [&] {
    openSelectedInstance();
  });

  connect(ui->rename, &QPushButton::clicked, [&] {
    rename();
  });
  connect(ui->exploreLocation, &QPushButton::clicked, [&] {
    exploreLocation();
  });
  connect(ui->exploreBaseDirectory, &QPushButton::clicked, [&] {
    exploreBaseDirectory();
  });
  connect(ui->exploreGame, &QPushButton::clicked, [&] {
    exploreGame();
  });

  connect(ui->convertToGlobal, &QPushButton::clicked, [&] {
    convertToGlobal();
  });
  connect(ui->convertToPortable, &QPushButton::clicked, [&] {
    convertToPortable();
  });
  connect(ui->openINI, &QPushButton::clicked, [&] {
    openINI();
  });
  connect(ui->deleteInstance, &QPushButton::clicked, [&] {
    deleteInstance();
  });

  connect(ui->switchToInstance, &QPushButton::clicked, [&] {
    openSelectedInstance();
  });
  connect(ui->close, &QPushButton::clicked, [&] {
    close();
  });

  selectActiveInstance();
}

void InstanceManagerDialog::showEvent(QShowEvent* e)
{
  // there might not be a global Settings object if this is called on startup
  // when there's no current instance
  const auto* s = Settings::maybeInstance();

  if (s) {
    s->geometry().restoreGeometry(this);
  }

  QDialog::showEvent(e);
}

void InstanceManagerDialog::done(int r)
{
  // there might not be a global Settings object if this is called on startup
  // when there's no current instance
  auto* s = Settings::maybeInstance();

  if (s) {
    s->geometry().saveGeometry(this);
  }

  QDialog::done(r);
}

void InstanceManagerDialog::updateInstances()
{
  auto& m = InstanceManager::singleton();

  m_instances.clear();

  for (auto&& d : m.portableInstancePaths()) {
    m_instances.push_back(std::make_unique<Instance>(d, true));
  }

  for (auto&& d : m.globalInstancePaths()) {
    m_instances.push_back(std::make_unique<Instance>(d, false));
  }

  // Keep portable storage together, with the legacy root entry first.
  std::sort(m_instances.begin(), m_instances.end(), [](auto&& a, auto&& b) {
    if (a->isPortable() != b->isPortable()) {
      return a->isPortable();
    }

    const auto root = InstanceManager::singleton().portablePath();
    const auto normalizedRoot =
        QDir::cleanPath(QDir::fromNativeSeparators(root)).toCaseFolded();
    const auto normalizedA = QDir::cleanPath(
                                 QDir::fromNativeSeparators(a->directory()))
                                 .toCaseFolded();
    const auto normalizedB = QDir::cleanPath(
                                 QDir::fromNativeSeparators(b->directory()))
                                 .toCaseFolded();
    const bool aIsRoot = a->isPortable() && normalizedA == normalizedRoot;
    const bool bIsRoot = b->isPortable() && normalizedB == normalizedRoot;
    if (aIsRoot != bIsRoot) {
      return aIsRoot;
    }

    return (MOBase::naturalCompare(a->displayName(), b->displayName()) < 0);
  });

  // read all inis, ignore errors
  for (auto&& i : m_instances) {
    i->readFromIni();
  }
}

void InstanceManagerDialog::updateList()
{
  const auto prevSelIndex = singleSelectionIndex();
  const auto* prevSel     = singleSelection();

  m_model->clear();
  ui->instancesHeading->setText(
      tr("Available instances (%1)").arg(m_instances.size()));

  std::size_t sel = NoSelection;

  // creating items for instances
  for (std::size_t i = 0; i < m_instances.size(); ++i) {
    const auto& ii = *m_instances[i];

    auto* item = new QStandardItem(ii.displayName());
    item->setIcon(instanceIcon(m_pc, ii));
    item->setSizeHint(QSize(220, 48));
    item->setToolTip(
        tr("%1\nGame: %2\nLocation: %3")
            .arg(ii.isPortable() ? tr("Portable instance")
                                 : tr("Global instance"),
                 ii.gameName(), ii.directory()));

    m_model->appendRow(item);

    if (&ii == prevSel) {
      sel = i;
    }
  }

  // keep current selection or select the next one if there was a selection;
  // there's no selection when opening the dialog, that's handled in the ctor
  if (prevSel) {
    if (m_instances.empty()) {
      select(-1);
    } else {
      if (sel == NoSelection) {
        if (prevSelIndex >= m_instances.size()) {
          sel = m_instances.size() - 1;
        } else {
          sel = prevSelIndex;
        }
      }

      select(sel);
    }
  }
}

void InstanceManagerDialog::select(std::size_t i)
{
  if (i < m_instances.size()) {
    const auto& ii = m_instances[i];
    fillData(*ii);

    ui->list->selectionModel()->select(
        m_filter.mapFromSource(m_filter.sourceModel()->index(i, 0)),
        QItemSelectionModel::ClearAndSelect);
  } else {
    clearData();
  }
}

void InstanceManagerDialog::select(const QString& name)
{
  for (std::size_t i = 0; i < m_instances.size(); ++i) {
    if (m_instances[i]->displayName() == name) {
      select(i);
      return;
    }
  }

  log::error("can't select instance {}, not in list", name);
}

void InstanceManagerDialog::selectActiveInstance()
{
  const auto active = InstanceManager::singleton().currentInstance();

  if (active) {
    for (std::size_t i = 0; i < m_instances.size(); ++i) {
      if (m_instances[i]->instanceIdentifier() ==
          active->instanceIdentifier()) {
        select(i);

        ui->list->scrollTo(m_filter.mapFromSource(m_filter.sourceModel()->index(i, 0)));

        return;
      }
    }
  }

  if (!m_instances.empty()) {
    select(0);
    ui->list->scrollTo(m_filter.mapFromSource(m_filter.sourceModel()->index(0, 0)));
    return;
  }

  clearData();
}

void InstanceManagerDialog::openSelectedInstance()
{
  const auto i = singleSelectionIndex();
  if (i == NoSelection) {
    return;
  }

  const auto& to = *m_instances[i];

  if (to.isActive()) {
    accept();
    return;
  }

  if (!confirmSwitch(to)) {
    return;
  }

  InstanceManager::singleton().setCurrentInstance(to);

  if (m_restartOnSelect) {
    ExitModOrganizer(Exit::Restart);
  }

  accept();
}

bool InstanceManagerDialog::confirmSwitch(const Instance& to)
{
  // there might not be a global Settings object if this is called on startup
  // when there's no current instance
  const auto* s = Settings::maybeInstance();

  // if there is are no settings, no instances are loaded and the confirmation
  // wouldn't make sense
  if (!s) {
    return true;
  }

  if (!s->interface().showChangeGameConfirmation()) {
    // user disabled confirmation
    return true;
  }

  QDialog dlg(this);
  dlg.setObjectName(QStringLiteral("SwitchInstanceDialog"));
  dlg.setWindowTitle(tr("Switching instances"));
  dlg.setMinimumWidth(540);
  dlg.resize(580, 290);

  auto* layout = new QVBoxLayout(&dlg);
  layout->setContentsMargins(20, 18, 20, 18);
  layout->setSpacing(12);

  auto* header = new QFrame(&dlg);
  header->setObjectName(QStringLiteral("switchInstanceHeader"));
  auto* headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(14, 13, 16, 13);
  headerLayout->setSpacing(12);

  auto* badge = new QFrame(header);
  badge->setObjectName(QStringLiteral("switchInstanceBadge"));
  badge->setFixedSize(42, 42);
  auto* badgeLayout = new QVBoxLayout(badge);
  badgeLayout->setContentsMargins(0, 0, 0, 0);
  auto* mark = new QLabel(QStringLiteral("i"), badge);
  mark->setObjectName(QStringLiteral("switchInstanceMark"));
  mark->setAlignment(Qt::AlignCenter);
  badgeLayout->addWidget(mark);

  auto* headerText = new QWidget(header);
  headerText->setObjectName(QStringLiteral("switchInstanceHeaderText"));
  auto* headerTextLayout = new QVBoxLayout(headerText);
  headerTextLayout->setContentsMargins(0, 0, 0, 0);
  headerTextLayout->setSpacing(4);

  auto* heading = new QLabel(
      tr("Mod Organizer must restart to manage the instance '%1'.")
          .arg(to.displayName()),
      headerText);
  heading->setObjectName(QStringLiteral("switchInstanceHeading"));
  heading->setWordWrap(true);
  auto* description = new QLabel(
      tr("The selected instance will open after Mod Organizer restarts."),
      headerText);
  description->setObjectName(QStringLiteral("switchInstanceDescription"));
  description->setWordWrap(true);
  headerTextLayout->addWidget(heading);
  headerTextLayout->addWidget(description);
  headerLayout->addWidget(badge);
  headerLayout->addWidget(headerText, 1);
  layout->addWidget(header);

  auto* settingsHint = new QFrame(&dlg);
  settingsHint->setObjectName(QStringLiteral("switchInstanceSettingsHint"));
  auto* settingsLayout = new QHBoxLayout(settingsHint);
  settingsLayout->setContentsMargins(12, 9, 12, 9);
  auto* settingsText = new QLabel(
      tr("You can turn off this confirmation in Settings."), settingsHint);
  settingsText->setObjectName(QStringLiteral("switchInstanceSettingsText"));
  settingsText->setWordWrap(true);
  settingsLayout->addWidget(settingsText);
  layout->addWidget(settingsHint);
  layout->addStretch(1);

  auto* buttonLayout = new QHBoxLayout;
  buttonLayout->setSpacing(8);
  buttonLayout->addStretch(1);
  auto* cancelButton = new QPushButton(tr("Cancel"), &dlg);
  cancelButton->setObjectName(QStringLiteral("switchInstanceCancelButton"));
  auto* restartButton = new QPushButton(tr("Restart Mod Organizer"), &dlg);
  restartButton->setObjectName(QStringLiteral("switchInstanceRestartButton"));
  restartButton->setDefault(true);
  buttonLayout->addWidget(cancelButton);
  buttonLayout->addWidget(restartButton);
  layout->addLayout(buttonLayout);

  connect(cancelButton, &QPushButton::clicked, &dlg, &QDialog::reject);
  connect(restartButton, &QPushButton::clicked, &dlg, &QDialog::accept);
  return (dlg.exec() == QDialog::Accepted);
}

void InstanceManagerDialog::rename()
{
  auto* i = singleSelection();
  if (!i) {
    return;
  }

  const auto selIndex = singleSelectionIndex();

  auto& m = InstanceManager::singleton();
  if (i->isActive()) {
    QMessageBox::information(this, tr("Rename instance"),
                             tr("The active instance cannot be renamed."));
    return;
  }

  const bool isRootPortable =
      i->isPortable() &&
      QDir::cleanPath(QDir::fromNativeSeparators(i->directory()))
              .compare(QDir::cleanPath(QDir::fromNativeSeparators(m.portablePath())),
                       Qt::CaseInsensitive) == 0;
  if (isRootPortable) {
    QMessageBox::information(
        this, tr("Rename instance"),
        tr("The portable instance in the MO2 installation folder cannot be renamed."));
    return;
  }

  // getting new name
  const auto parentDirectory = QFileInfo(i->directory()).dir().absolutePath();
  const auto oldName = QFileInfo(i->directory()).fileName();
  const auto newName = getInstanceName(this, tr("Rename instance"), "",
                                       tr("Instance name"), parentDirectory,
                                       oldName);

  if (newName.isEmpty()) {
    return;
  }

  // renaming
  const QString src = i->directory();
  const QString dest =
      QDir::toNativeSeparators(QFileInfo(src).dir().path() + "/" + newName);

  log::info("renaming {} to {}", src, dest);

  const auto r = shell::Rename(QFileInfo(src), QFileInfo(dest), false);

  if (!r) {
    QMessageBox::critical(this, tr("Error"),
                          tr("Failed to rename \"%1\" to \"%2\": %3")
                              .arg(src)
                              .arg(dest)
                              .arg(r.toString()));

    return;
  }

  // updating ui
  auto newInstance = std::make_unique<Instance>(dest, i->isPortable());
  i                = newInstance.get();

  m_model->item(selIndex)->setText(newInstance->displayName());
  m_instances[selIndex] = std::move(newInstance);

  fillData(*i);
}

void InstanceManagerDialog::exploreLocation()
{
  if (const auto* i = singleSelection()) {
    shell::Explore(i->directory());
  }
}

void InstanceManagerDialog::exploreBaseDirectory()
{
  if (const auto* i = singleSelection()) {
    shell::Explore(i->baseDirectory());
  }
}

void InstanceManagerDialog::exploreGame()
{
  if (const auto* i = singleSelection()) {
    shell::Explore(i->gameDirectory());
  }
}

void InstanceManagerDialog::openINI()
{
  if (const auto* i = singleSelection()) {
    shell::Open(i->iniPath());
  }
}

void InstanceManagerDialog::deleteInstance()
{
  const auto* i = singleSelection();
  if (!i) {
    return;
  }

  auto& m = InstanceManager::singleton();
  if (i->isActive()) {
    QMessageBox::information(this, tr("Deleting instance"),
                             tr("The active instance cannot be deleted."));
    return;
  }

  const auto Recycle = QMessageBox::Save;
  const auto Delete  = QMessageBox::Yes;
  const auto Cancel  = QMessageBox::Cancel;

  const auto files = i->objectsForDeletion();

  QDialog dlg(this);
  dlg.setObjectName(QStringLiteral("DeleteInstanceDialog"));
  dlg.setWindowTitle(tr("Deleting instance"));
  dlg.setMinimumSize(650, 450);
  dlg.resize(720, 500);

  auto* dialogLayout = new QVBoxLayout(&dlg);
  dialogLayout->setContentsMargins(20, 18, 20, 18);
  dialogLayout->setSpacing(12);

  auto* header = new QFrame(&dlg);
  header->setObjectName(QStringLiteral("deleteInstanceHeader"));
  auto* headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(14, 13, 16, 13);
  headerLayout->setSpacing(12);

  auto* warningBadge = new QFrame(header);
  warningBadge->setObjectName(QStringLiteral("deleteWarningBadge"));
  warningBadge->setFixedSize(42, 42);
  auto* badgeLayout = new QVBoxLayout(warningBadge);
  badgeLayout->setContentsMargins(0, 0, 0, 0);
  auto* warningMark = new QLabel(QStringLiteral("!"), warningBadge);
  warningMark->setObjectName(QStringLiteral("deleteWarningMark"));
  warningMark->setAlignment(Qt::AlignCenter);
  badgeLayout->addWidget(warningMark);

  auto* headerText = new QWidget(header);
  headerText->setObjectName(QStringLiteral("deleteInstanceHeaderText"));
  auto* headerTextLayout = new QVBoxLayout(headerText);
  headerTextLayout->setContentsMargins(0, 0, 0, 0);
  headerTextLayout->setSpacing(3);
  auto* heading = new QLabel(tr("These files and folders will be deleted"), headerText);
  heading->setObjectName(QStringLiteral("deleteInstanceHeading"));
  auto* description = new QLabel(
      tr("Only checked paths will be removed. Required items stay selected."),
      headerText);
  description->setObjectName(QStringLiteral("deleteInstanceDescription"));
  description->setWordWrap(true);
  headerTextLayout->addWidget(heading);
  headerTextLayout->addWidget(description);
  headerLayout->addWidget(warningBadge);
  headerLayout->addWidget(headerText, 1);
  dialogLayout->addWidget(header);

  auto* listCard = new QFrame(&dlg);
  listCard->setObjectName(QStringLiteral("deleteInstanceListCard"));
  auto* listLayout = new QVBoxLayout(listCard);
  listLayout->setContentsMargins(12, 10, 12, 12);
  listLayout->setSpacing(7);

  auto* listHeader = new QHBoxLayout;
  auto* listTitle = new QLabel(tr("Items to remove"), listCard);
  listTitle->setObjectName(QStringLiteral("deleteInstanceListTitle"));
  const auto itemCountValue = static_cast<qulonglong>(files.size());
  const auto itemCountText =
      itemCountValue == 1 ? tr("%1 item").arg(itemCountValue)
                          : tr("%1 items").arg(itemCountValue);
  auto* itemCount = new QLabel(itemCountText, listCard);
  itemCount->setObjectName(QStringLiteral("deleteInstanceItemCount"));
  listHeader->addWidget(listTitle);
  listHeader->addStretch(1);
  listHeader->addWidget(itemCount);
  listLayout->addLayout(listHeader);

  auto* list = new QListWidget(listCard);
  list->setObjectName(QStringLiteral("deleteInstancePaths"));
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  list->setAlternatingRowColors(true);
  list->setSelectionMode(QAbstractItemView::NoSelection);
  list->setMinimumHeight(145);
  list->setMaximumHeight(250);

  for (const auto& f : files) {
    auto* item = new QListWidgetItem(list);
    item->setData(Qt::UserRole, f.path);

    auto* pathCheck = new QCheckBox(f.path, list);
    pathCheck->setObjectName(QStringLiteral("deleteInstancePathCheck"));
    pathCheck->setToolTip(f.path);
    pathCheck->setChecked(f.mandatoryDelete);
    pathCheck->setEnabled(!f.mandatoryDelete);
    pathCheck->setMinimumHeight(32);
    item->setSizeHint(pathCheck->sizeHint());
    list->setItemWidget(item, pathCheck);
  }

  if (list->count() == 0) {
    auto* emptyItem = new QListWidgetItem(tr("No files or folders were found."));
    emptyItem->setFlags(emptyItem->flags() & (~Qt::ItemIsEnabled));
    list->addItem(emptyItem);
  }

  listLayout->addWidget(list, 1);
  dialogLayout->addWidget(listCard, 1);

  auto* actionHint = new QLabel(tr("Choose how MO2 should remove the checked items."), &dlg);
  actionHint->setObjectName(QStringLiteral("deleteInstanceActionHint"));
  dialogLayout->addWidget(actionHint);

  auto* buttonLayout = new QHBoxLayout;
  buttonLayout->setSpacing(8);
  auto* cancelButton = new QPushButton(tr("Cancel"), &dlg);
  cancelButton->setObjectName(QStringLiteral("deleteInstanceCancelButton"));
  cancelButton->setDefault(true);
  auto* permanentButton = new QPushButton(tr("Delete permanently"), &dlg);
  permanentButton->setObjectName(QStringLiteral("deleteInstancePermanentButton"));
  auto* recycleButton = new QPushButton(tr("Move to the recycle bin"), &dlg);
  recycleButton->setObjectName(QStringLiteral("deleteInstanceRecycleButton"));
  buttonLayout->addWidget(cancelButton);
  buttonLayout->addStretch(1);
  buttonLayout->addWidget(permanentButton);
  buttonLayout->addWidget(recycleButton);
  dialogLayout->addLayout(buttonLayout);

  connect(cancelButton, &QPushButton::clicked, &dlg, [&dlg, Cancel] {
    dlg.done(Cancel);
  });
  connect(permanentButton, &QPushButton::clicked, &dlg, [&dlg, Delete] {
    dlg.done(Delete);
  });
  connect(recycleButton, &QPushButton::clicked, &dlg, [&dlg, Recycle] {
    dlg.done(Recycle);
  });

  const auto r = dlg.exec();

  if (r != Recycle && r != Delete) {
    return;
  }

  // gathering all the selected items
  QStringList selected;

  for (int i = 0; i < list->count(); ++i) {
    auto* pathCheck = qobject_cast<QCheckBox*>(list->itemWidget(list->item(i)));
    if (pathCheck && pathCheck->isChecked()) {
      selected.append(list->item(i)->data(Qt::UserRole).toString());
    }
  }

  if (selected.isEmpty()) {
    QMessageBox::information(this, tr("Deleting instance"), tr("Nothing to delete."));

    return;
  }

  // deleting
  if (!doDelete(selected, (r == Recycle))) {
    return;
  }

  // updating ui
  updateInstances();
  updateList();
}

void InstanceManagerDialog::setRestartOnSelect(bool b)
{
  m_restartOnSelect = b;
}

bool InstanceManagerDialog::doDelete(const QStringList& files, bool recycle)
{
  // logging
  for (auto&& f : files) {
    if (recycle) {
      log::info("will recycle {}", f);
    } else {
      log::info("will delete {}", f);
    }
  }

  if (MOBase::shellDelete(files, recycle, this)) {
    return true;
  }

  const auto e = GetLastError();
  if (e == ERROR_CANCELLED) {
    log::debug("deletion cancelled by user");
  } else {
    log::error("failed to delete, {}", formatSystemMessage(e));
  }

  return false;
}

void InstanceManagerDialog::convertToGlobal()
{
  // not implemented
}

void InstanceManagerDialog::convertToPortable()
{
  // not implemented
}

void InstanceManagerDialog::onSelection()
{
  const auto i = singleSelectionIndex();
  if (i == NoSelection) {
    clearData();
    return;
  }

  select(i);
}

void InstanceManagerDialog::createNew()
{
  if (InstanceManager::singleton().isEldenRingOnlyPortableMode()) {
    return;
  }

  // there might not be settings available; the dialog can be shown when the
  // last selected instance doesn't exist anymore
  CreateInstanceDialog dlg(m_pc, Settings::maybeInstance(), this);

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  if (dlg.switching()) {
    // restarting MO
    accept();
    return;
  }

  updateInstances();
  updateList();

  const auto info = dlg.creationInfo();
  Instance created(info.dataPath, info.type == CreateInstanceDialog::Portable);
  for (std::size_t i = 0; i < m_instances.size(); ++i) {
    if (m_instances[i]->instanceIdentifier() ==
        created.instanceIdentifier()) {
      select(i);
      break;
    }
  }
}

std::size_t InstanceManagerDialog::singleSelectionIndex() const
{
  const auto sel =
      m_filter.mapSelectionToSource(ui->list->selectionModel()->selection());

  if (sel.size() != 1) {
    return NoSelection;
  }

  return static_cast<std::size_t>(sel.indexes()[0].row());
}

const Instance* InstanceManagerDialog::singleSelection() const
{
  const auto i = singleSelectionIndex();
  if (i == NoSelection) {
    return nullptr;
  }

  return m_instances[i].get();
}

void InstanceManagerDialog::fillData(const Instance& ii)
{
  ui->selectionHint->setVisible(false);
  ui->widget_7->setVisible(true);
  ui->widget_9->setVisible(true);

  ui->name->setText(ii.displayName());
  ui->location->setText(ii.directory());
  ui->baseDirectory->setText(ii.baseDirectory());
  ui->gameName->setText(ii.gameName());
  ui->gameDir->setText(ii.gameDirectory());
  setButtonsEnabled(true);

  const auto& m = InstanceManager::singleton();

  ui->instanceStatus->setText(
      QStringLiteral("%1  |  %2")
          .arg(ii.isPortable() ? tr("Portable storage") : tr("Global storage"),
               ii.isActive() ? tr("Current instance") : tr("Ready to switch")));

  const bool active = ii.isActive();
  ui->switchToInstance->setEnabled(!active);
  ui->switchToInstance->setText(active ? tr("Current instance")
                                       : tr("Switch to this instance"));

  const auto portableRoot = QDir::cleanPath(
      QDir::fromNativeSeparators(m.portablePath()));
  const auto instanceRoot = QDir::cleanPath(
      QDir::fromNativeSeparators(ii.directory()));
  const bool isRootPortable =
      ii.isPortable() &&
      portableRoot.compare(instanceRoot, Qt::CaseInsensitive) == 0;
  ui->rename->setEnabled(!isRootPortable);
  if (m.isEldenRingOnlyPortableMode()) {
    ui->deleteInstance->setEnabled(false);
  }

  if (ii.isPortable()) {
    ui->convertToPortable->setVisible(false);
    ui->convertToGlobal->setVisible(true);
    ui->convertToGlobal->setEnabled(true);
  } else {
    ui->convertToPortable->setVisible(true);
    ui->convertToGlobal->setVisible(false);

    if (m.portableInstanceExists()) {
      ui->convertToPortable->setEnabled(false);
      ui->convertToPortable->setToolTip(tr("A portable instance already exists."));
    } else {
      ui->convertToPortable->setEnabled(false);
      ui->convertToPortable->setToolTip("");
    }
  }

  // not implemented, hide the buttons
  ui->convertToPortable->setVisible(false);
  ui->convertToGlobal->setVisible(false);
}

void InstanceManagerDialog::clearData()
{
  ui->selectionHint->setVisible(true);
  ui->widget_7->setVisible(false);
  ui->widget_9->setVisible(false);
  ui->instanceStatus->clear();
  ui->name->clear();
  ui->location->clear();
  ui->baseDirectory->clear();
  ui->gameName->clear();
  ui->gameDir->clear();

  setButtonsEnabled(false);

  ui->convertToPortable->setVisible(false);
  ui->convertToGlobal->setVisible(false);
}

void InstanceManagerDialog::setButtonsEnabled(bool b)
{
  ui->rename->setEnabled(b);
  ui->exploreLocation->setEnabled(b);
  ui->exploreBaseDirectory->setEnabled(b);
  ui->exploreGame->setEnabled(b);
  ui->convertToPortable->setEnabled(b);
  ui->convertToGlobal->setEnabled(b);
  ui->deleteInstance->setEnabled(b);
  ui->openINI->setEnabled(b);
  ui->switchToInstance->setEnabled(b);
}
