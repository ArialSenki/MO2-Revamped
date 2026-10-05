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
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSize>
#include <QSizePolicy>

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
  dlg.setWindowTitle(title);

  auto* ly = new QVBoxLayout(&dlg);

  auto* bb = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

  auto* text = new QLineEdit(oldName);
  text->selectAll();

  auto* error = new QLabel;

  if (!moreText.isEmpty()) {
    auto* lb = new QLabel(moreText);
    lb->setWordWrap(true);
    ly->addWidget(lb);
    ly->addSpacing(10);
  }

  auto* lb = new QLabel(label);
  lb->setWordWrap(true);
  ly->addWidget(lb);

  ly->addWidget(text);
  ly->addWidget(error);
  ly->addStretch();
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

    error->setVisible(!okay);
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

  dlg.resize({400, 120});
  if (dlg.exec() != QDialog::Accepted) {
    return {};
  }

  return MOBase::sanitizeFileName(text->text());
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
  ui->openINI->setToolTip(
      tr("Open this instance's ModOrganizer.ini with the associated editor."));
  ui->switchToInstance->setToolTip(
      tr("Restart Mod Organizer and open the selected instance."));
  ui->list->setStyleSheet(
      QStringLiteral("QListView::item:selected { "
                     "background-color: palette(button); "
                     "color: palette(text); "
                     "border: 1px solid palette(mid); }"));
  ui->detailsHeading->setAlignment(Qt::AlignHCenter);
  ui->selectionHint->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  ui->instanceStatus->setAlignment(Qt::AlignHCenter);
  ui->widget_7->setMaximumWidth(580);
  ui->widget_9->setMaximumWidth(580);
  ui->widget_7->setMinimumWidth(520);
  for (QLineEdit* field : ui->widget_7->findChildren<QLineEdit*>()) {
    field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }
  if (auto* detailsLayout = qobject_cast<QGridLayout*>(ui->widget_7->layout())) {
    detailsLayout->setContentsMargins(10, 6, 10, 6);
    detailsLayout->setHorizontalSpacing(14);
    detailsLayout->setVerticalSpacing(10);
  }
  for (QLabel* label : ui->widget_7->findChildren<QLabel*>()) {
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  }
  if (auto* actionLayout = qobject_cast<QHBoxLayout*>(ui->widget_9->layout())) {
    actionLayout->insertStretch(0, 1);
  }
  ui->verticalLayout_2->setAlignment(ui->widget_7, Qt::AlignHCenter);
  ui->verticalLayout_2->setAlignment(ui->widget_9, Qt::AlignHCenter);

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

  MOBase::TaskDialog dlg(this);

  const auto r = dlg.title(tr("Switching instances"))
                     .main(tr("Mod Organizer must restart to manage the instance '%1'.")
                               .arg(to.displayName()))
                     .content(tr("This confirmation can be disabled in the settings."))
                     .icon(QMessageBox::Question)
                     .button({tr("Restart Mod Organizer"), QMessageBox::Ok})
                     .button({tr("Cancel"), QMessageBox::Cancel})
                     .exec();

  return (r == QMessageBox::Ok);
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

  // creating dialog

  const auto Recycle = QMessageBox::Save;
  const auto Delete  = QMessageBox::Yes;
  const auto Cancel  = QMessageBox::Cancel;

  const auto files = i->objectsForDeletion();

  MOBase::TaskDialog dlg(this);

  dlg.title(tr("Deleting instance"))
      .main(tr("These files and folders will be deleted"))
      .content(tr("All checked items will be deleted."))
      .icon(QMessageBox::Warning)
      .button({tr("Move to the recycle bin"), Recycle})
      .button({tr("Delete permanently"), Delete})
      .button({tr("Cancel"), Cancel});

  auto* list = new QListWidget();
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  list->setMaximumHeight(160);

  // filling the list
  for (const auto& f : files) {
    auto* item = new QListWidgetItem(f.path);

    if (f.mandatoryDelete) {
      // disable, cannot uncheck mandatory items
      item->setFlags(item->flags() & (~Qt::ItemIsEnabled));

      // checked by default
      item->setCheckState(Qt::Checked);
    } else {
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

      // unchecked by default
      item->setCheckState(Qt::Unchecked);
    }

    list->addItem(item);
  }

  dlg.addContent(list);
  dlg.setWidth(600);

  const auto r = dlg.exec();

  if (r != Recycle && r != Delete) {
    return;
  }

  // gathering all the selected items
  QStringList selected;

  for (int i = 0; i < list->count(); ++i) {
    if (list->item(i)->checkState() == Qt::Checked) {
      selected.append(list->item(i)->text());
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
