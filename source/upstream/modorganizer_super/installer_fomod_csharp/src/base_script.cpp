/*
Copyright (C) 2020 Holt59. All rights reserved.

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

// clang-format off

#include <memory>
#include "base_script.h"

#include <filesystem>
#include <map>

#include <QButtonGroup>
#include <QCoreApplication>
#include <QListWidget>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QCheckBox>
#include <QRadioButton>
#include <QInputDialog>
#include <QSettings>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVersionNumber>
#include <QLabel>
#include <QVBoxLayout>
#include <QApplication>
#include <Windows.h>

#include "imoinfo.h"
#include "iplugingame.h"
#include "ipluginlist.h"
#include "igamefeatures.h"
#include "iinstallationmanager.h"
#include "log.h"
#include "scriptextender.h"

#include "psettings.h"
#include "installer_fomod_postdialog.h"
#include "fluentcontentdialog.h"
#include "csharp_interface.h"
#include "csharp_utils.h"

using namespace MOBase;

namespace CSharp {

  // Pointer to object:
  static MOBase::IOrganizer* g_Organizer = nullptr;

  // Per-install globals:
  struct Globals {
    IInstallationManager* InstallManager;
    QWidget* ParentWidget;
    std::shared_ptr<const IFileTree> SourceTree;
    std::shared_ptr<IFileTree> DestinationTree;

    // Map from path in destination entry to the original entry:
    std::map<std::shared_ptr<const FileTreeEntry>,
      std::shared_ptr<const FileTreeEntry>> InstalledEntries;

    // Map extracted entries (in the original tree) to (temporary) paths:
    std::map<std::shared_ptr<const FileTreeEntry>, QString> ExtractedEntries;

    // Map creted entries (in the destination tree) to (temporary) paths:
    std::map<std::shared_ptr<const FileTreeEntry>, QString> CreatedEntries;

    // List of modified settings values:
    std::map<QString, PSettings> Settings;

    Globals() { }
    Globals(
      IPlugin const* plugin, MOBase::IInstallationManager* manager, QWidget* parentWidget,
      std::shared_ptr<MOBase::IFileTree> tree, std::map<std::shared_ptr<const FileTreeEntry>, QString> entries) :
      m_Plugin(plugin), InstallManager(manager), ParentWidget(parentWidget), SourceTree(tree), DestinationTree(tree->createOrphanTree()), ExtractedEntries(std::move(entries)) {

    }

    Globals(Globals const&) = delete;
    Globals(Globals&&) = default;

    Globals& operator=(Globals const&) = delete;
    Globals& operator=(Globals&&) = default;

  private:
    IPlugin const* m_Plugin;
  };
  static Globals g;



  void init(MOBase::IOrganizer* moInfo) {

    // Do this only once:
    if (g_Organizer == nullptr) {
      Application::EnableVisualStyles();
      Application::SetCompatibleTextRenderingDefault(false);
    }

    g_Organizer = moInfo;
  }

  void beforeInstall(IPlugin const* plugin, MOBase::IInstallationManager* manager, QWidget* parentWidget,
    std::shared_ptr<MOBase::IFileTree> tree, std::map<std::shared_ptr<const FileTreeEntry>, QString> entries) {
    g = { plugin, manager, parentWidget, tree, std::move(entries) };
  }

  IPluginInstaller::EInstallResult postInstall(std::shared_ptr<MOBase::IFileTree>& tree) {

    if (!g.Settings.empty()) {

      // owned here: 2.5.2 never deleted it
      auto dialog = std::make_unique<InstallerFomodPostDialog>(g.ParentWidget);

      dialog->setIniSettings(g.Settings);

      // Installation cancelled:
      if (dialog->exec() == QDialog::Rejected) {
        return IPluginInstaller::EInstallResult::RESULT_CANCELED;
      }

      switch (dialog->result()) {

        // Discard, nothing do to:
      case InstallerFomodPostDialog::Result::DISCARD: break;

        // Apply, must fetch the profile INI settings and apply the settings:
      case InstallerFomodPostDialog::Result::APPLY: {
        for (auto& p : g.Settings) {
          QDir path(g_Organizer->profilePath());
          if (!g_Organizer->profile()->localSettingsEnabled()) {
            path = QDir(g_Organizer->managedGame()->documentsDirectory());
          }

          QSettings settings(path.filePath(p.first), QSettings::IniFormat);

          if (settings.status() != QSettings::NoError) {
            return IPluginInstaller::EInstallResult::RESULT_FAILED;
          }

          p.second.update(settings);
        }
      } break;

                                                  // Move, must create the INI files and apply the settings:
      case InstallerFomodPostDialog::Result::MOVE: {
        for (auto& p : g.Settings) {
          auto e = g.DestinationTree->addFile("INI Tweaks/" + p.first, false);
          if (e == nullptr) {
            return IPluginInstaller::EInstallResult::RESULT_FAILED;
          }
          QString path = g.InstallManager->createFile(e);
          if (path.isEmpty()) {
            return IPluginInstaller::EInstallResult::RESULT_FAILED;
          }
          QSettings settings(path, QSettings::IniFormat);
          if (settings.status() != QSettings::NoError) {
            return IPluginInstaller::EInstallResult::RESULT_FAILED;
          }
          p.second.update(settings);
        }
      } break;
      }

    }

    tree = g.DestinationTree;

    // Clear up:
    g = Globals();


    return IPluginInstaller::EInstallResult::RESULT_SUCCESS;
  }

}

namespace {

// MO2 Revamped: in the Select dialog with check boxes, a click on an option's
// name ticks it too, as the FOMOD installer's check boxes do. An event filter
// on the list, not signals: under /clr connecting a signal by function pointer
// finds no signal. A click on the box is ticked by Qt while it handles the
// release, so the state is checked after that (queued) and nothing toggles twice
class SelectNameClicks : public QObject
{
public:
  explicit SelectNameClicks(QListWidget* list) : QObject(list), m_list(list) {}

  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (event->type() == QEvent::MouseButtonPress) {
      const auto* mouse = static_cast<QMouseEvent*>(event);
      m_item  = mouse->button() == Qt::LeftButton
                    ? m_list->itemAt(mouse->position().toPoint())
                    : nullptr;
      m_state = m_item != nullptr ? m_item->checkState() : Qt::Unchecked;
    } else if (event->type() == QEvent::MouseButtonRelease && m_item != nullptr) {
      const auto* mouse   = static_cast<QMouseEvent*>(event);
      QListWidgetItem* item = m_item;
      const Qt::CheckState pressed = m_state;
      m_item = nullptr;
      if (mouse->button() == Qt::LeftButton &&
          m_list->itemAt(mouse->position().toPoint()) == item) {
        QMetaObject::invokeMethod(
            this,
            [item, pressed] {
              if (item->checkState() == pressed) {
                item->setCheckState(pressed == Qt::Checked ? Qt::Unchecked
                                                           : Qt::Checked);
              }
            },
            Qt::QueuedConnection);
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QListWidget* m_list;
  QListWidgetItem* m_item = nullptr;
  Qt::CheckState m_state  = Qt::Unchecked;
};

}  // namespace

namespace CSharp {

  using namespace System::IO;

  bool isFomodEntry(std::shared_ptr<const FileTreeEntry> entry) {
    return entry->pathFrom(g.SourceTree).compare("fomod", Qt::CaseInsensitive) == 0;
  }

  bool BaseScriptImpl::PerformBasicInstall() {
    for (auto e : *g.SourceTree) {
      if (!isFomodEntry(e)) {
        auto ce = g.DestinationTree->copy(e, "", IFileTree::InsertPolicy::MERGE);
        g.InstalledEntries[ce] = e;
      }
    }
    return true;
  }

  bool BaseScriptImpl::InstallFileFromMod(String^ p_strFrom, String^ p_strTo) {
    auto sourceEntry = g.SourceTree->find(to_qstring(p_strFrom));

    if (!sourceEntry) {
      log::warn("File '{}' not found in the archive.", to_wstring(p_strFrom));
      return false;
    }

    if (auto ce = g.DestinationTree->copy(sourceEntry, to_qstring(p_strTo)); ce != nullptr) {
      g.InstalledEntries[ce] = sourceEntry;
      return true;
    }

    return false;
  }

  array<String^>^ BaseScriptImpl::GetModFileList() {
    // Cannot directly fill a, e.g., List<String^>^ because I cannot capture it:
    std::vector<QString> paths;
    g.SourceTree->walk([&](QString const& path, std::shared_ptr<const FileTreeEntry> entry) {
      // Discard fomod folder:
      if (isFomodEntry(entry)) {
        return IFileTree::WalkReturn::SKIP;
      }
      if (entry->isFile()) {
        paths.push_back(path + entry->name());
      }
      return IFileTree::WalkReturn::CONTINUE;
      }, "/");

    // Convert to C#:
    array<String^>^ result = gcnew array<String^>(paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i) {
      result[i] = from_string(paths[i].toStdWString());
    }

    return result;
  }


  /**
   * @brief Extract the given entry.
   *
   * If the entry has already been extracted, the existing paths is returned.
   *
   * @param entry The entry to extract.
   *
   * @return path to the temporary file corresponding to the entry.
   */
  QString extractFile(std::shared_ptr<const FileTreeEntry> entry) {

    QString qPath;
    if (auto it = g.ExtractedEntries.find(entry); it != g.ExtractedEntries.end()) {
      qPath = it->second;
    }
    else {
      qPath = g.InstallManager->extractFile(entry, true);

      if (qPath.isEmpty()) {
        return QString();
      }

      g.ExtractedEntries[entry] = qPath;
    }

    return qPath;
  }

  array<Byte>^ BaseScriptImpl::GetFileFromMod(String^ p_strFile) {
    auto entry = g.SourceTree->find(to_qstring(p_strFile));

    if (!entry) {
      return gcnew array<Byte>(0);
    }

    QString qPath = extractFile(entry);
    if (qPath.isEmpty()) {
      return gcnew array<Byte>(0);
    }

    String^ path = from_string(qPath.toStdWString());
    return File::ReadAllBytes(path);
  }

  QStringList getDataFiles(QString folder, QString pattern, bool allFolders) {
    QStringList files = g_Organizer->findFiles(folder, [pattern](QString const& filepath) {
      return QDir::match(pattern, QFileInfo(filepath).fileName());
      });
    if (allFolders) {
      QStringList directories = g_Organizer->listDirectories(folder);
      for (QString directory : directories) {
        // MO2 does not like path with . or / (I think), so creating the path manually:
        files.append(getDataFiles((folder.isEmpty() ? "" : folder + QDir::separator()) + directory, pattern, allFolders));
      }
    }
    return files;
  }

  array<String^>^ BaseScriptImpl::GetExistingDataFileList(String^ p_strPath, String^ p_strPattern, bool p_booAllFolders) {
    QStringList files = getDataFiles(to_qstring(p_strPath), to_qstring(p_strPattern), p_booAllFolders);
    array<String^>^ result = gcnew array<String^>(files.size());

    QDir modsDir(g_Organizer->modsPath());
    for (int i = 0; i < files.size(); ++i) {
      // We need to trim the path to the actual folder containing the mod (did not find a better way):
      QString rpath = modsDir.relativeFilePath(files[i]).replace("/", QDir::separator());
      result[i] = from_string(rpath.mid(rpath.indexOf(QDir::separator()) + 1).toStdWString());
    }
    return result;
  }

  /**
   * @brief Find the data-file path corresponding to the given path.
   *
   * This methods first lookup the file in the destination tree of the mod (for files
   * that have been extracted / created by the mod), and if it does not find it there
   * lookup files from other mods.
   *
   * @param p_strPath Path to the file to lookup, relative to the data folder.
   *
   * @return a path to an actual corresponding file, or a null pointer if the
   *   file was not found.
   */
  String^ getDataFilePath(String^ p_strPath) {

    // Check if the file is in the output tree:
    QString qPath = to_qstring(p_strPath);
    if (auto e = g.DestinationTree->find(qPath); e != nullptr) {

      // Check if it's a created entry:
      if (auto it = g.CreatedEntries.find(e); it != g.CreatedEntries.end()) {
        return from_string(g.CreatedEntries[e]);
      }

      // Find the source entry - We need to check for parent:
      std::shared_ptr<const FileTreeEntry> originalEntry;
      if (auto it = g.InstalledEntries.find(e); it != g.InstalledEntries.end()) {
        originalEntry = it->second;
      }
      else {
        std::shared_ptr<const IFileTree> tree = e->parent();
        decltype(tree) oTree = nullptr;
        while (oTree == nullptr && tree != nullptr) {
          auto it = g.InstalledEntries.find(tree);
          if (it != g.InstalledEntries.end()) {
            oTree = it->second->astree();
          }
          else {
            tree = tree->parent();
          }
        }
        originalEntry = oTree->find(e->pathFrom(tree));
      }
      QString path = extractFile(originalEntry);
      if (path.isEmpty()) {
        return nullptr;
      }
      return from_string(path);
    }

    // Convert to path and normalize separator:
    auto path = std::filesystem::path(qPath.toStdWString()).make_preferred();
    QStringList paths = g_Organizer->findFiles(ToQString(path.parent_path().native()), [name = ToQString(path.filename())](QString const& filepath) {
      return QFileInfo(filepath).fileName().compare(name, Qt::CaseInsensitive) == 0;
      });

    if (paths.isEmpty()) {
      return nullptr;
    }

    return from_string(paths[0]);
  }

  bool BaseScriptImpl::DataFileExists(String^ p_strPath) {
    return getDataFilePath(p_strPath) != nullptr;
  }

  array<Byte>^ BaseScriptImpl::GetExistingDataFile(String^ p_strPath) {

    // Convert to path and normalize separator:
    String^ datapath = getDataFilePath(p_strPath);

    if (datapath == nullptr) {
      return nullptr;
    }

    // Read the first file (should be only one):
    return File::ReadAllBytes(datapath);
  }

  bool BaseScriptImpl::GenerateDataFile(String^ p_strPath, array<Byte>^ p_bteData) {

    // Check if we already have created an entry for this:
    QString qPath = to_qstring(p_strPath);
    auto entry = g.DestinationTree->find(qPath);

    QString qAbsPath;
    // If the entry is in the list of created files (note: if the entry does not exist,
    // find return a nullptr, which is never in g.CreatedEntries).
    if (auto it = g.CreatedEntries.find(entry); it != g.CreatedEntries.end()) {
      qAbsPath = g.CreatedEntries.at(entry);
    }
    // Otherwize: Create the entry and the temporary file:
    else {
      auto entry = g.DestinationTree->addFile(qPath, true);
      qAbsPath = g.InstallManager->createFile(entry);
      if (qAbsPath.isEmpty()) {
        // Remove the entry from the tree:
        entry->detach();
        return false;
      }

      // Store the created entry:
      g.CreatedEntries[entry] = qAbsPath;
    }

    String^ absPath = from_string(qAbsPath);
    File::WriteAllBytes(absPath, p_bteData);
    return true;
  }

  // UI methods:

  // MO2 Revamped: a script's message and its choices are MO2's content
  // dialogs over the installer's window. 2.5.2 showed a plain message box
  // (whose warning icon a missing break dropped) and a bare list of check
  // boxes with each option's description only as a tooltip.

  namespace
  {
  QString scriptText(const char* text)
  {
    return QCoreApplication::translate("RevampedCSharpScript", text);
  }

  // a button of the band that ends the dialog with its result as its id
  void addResultButton(RevampedFluent::ContentDialog& dialog, QButtonGroup& group,
                       const char* text, int result, bool primary)
  {
    QPushButton* button = dialog.addButton(scriptText(text), primary);
    group.addButton(button, result);
  }
  }  // namespace

  DialogResult BaseScriptImpl::ExtendedMessageBox(String^ p_strMessage, String^ p_strTitle, String^ p_strDetails, MessageBoxButtons p_mbbButtons, MessageBoxIcon) {
    const QString title = String::IsNullOrEmpty(p_strTitle)
                              ? scriptText("Message from the installer")
                              : to_qstring(p_strTitle);
    RevampedFluent::ContentDialog dialog(g.ParentWidget, QStringLiteral("csharpScriptMessage"),
                                         title, to_qstring(p_strMessage));
    dialog.setMinimumWidth(480);

    if (!String::IsNullOrEmpty(p_strDetails)) {
      auto* details = new QPlainTextEdit(&dialog);
      details->setReadOnly(true);
      details->setPlainText(to_qstring(p_strDetails));
      details->setAccessibleName(scriptText("Details"));
      details->setMinimumHeight(120);
      dialog.body()->addWidget(details, 1);
    }

    const int abortId  = static_cast<int>(DialogResult::Abort);
    const int retryId  = static_cast<int>(DialogResult::Retry);
    const int ignoreId = static_cast<int>(DialogResult::Ignore);
    const int okId     = static_cast<int>(DialogResult::OK);
    const int cancelId = static_cast<int>(DialogResult::Cancel);
    const int yesId    = static_cast<int>(DialogResult::Yes);
    const int noId     = static_cast<int>(DialogResult::No);

    // done(id) ends the dialog with the clicked button's result
    QButtonGroup group(&dialog);
    switch (p_mbbButtons) {
    case MessageBoxButtons::AbortRetryIgnore:
      addResultButton(dialog, group, "Abort", abortId, false);
      addResultButton(dialog, group, "Retry", retryId, true);
      addResultButton(dialog, group, "Ignore", ignoreId, false);
      break;
    case MessageBoxButtons::OKCancel:
      addResultButton(dialog, group, "OK", okId, true);
      addResultButton(dialog, group, "Cancel", cancelId, false);
      break;
    case MessageBoxButtons::RetryCancel:
      addResultButton(dialog, group, "Retry", retryId, true);
      addResultButton(dialog, group, "Cancel", cancelId, false);
      break;
    case MessageBoxButtons::YesNoCancel:
      addResultButton(dialog, group, "Yes", yesId, true);
      addResultButton(dialog, group, "No", noId, false);
      addResultButton(dialog, group, "Cancel", cancelId, false);
      break;
    case MessageBoxButtons::YesNo:
      addResultButton(dialog, group, "Yes", yesId, true);
      addResultButton(dialog, group, "No", noId, false);
      break;
    default:
      addResultButton(dialog, group, "OK", okId, true);
      break;
    }
    // a string connect: C++/CLI finds no signal for a function pointer
    QObject::connect(&group, SIGNAL(idClicked(int)), &dialog, SLOT(done(int)));

    const int result = dialog.exec();
    if (result != QDialog::Rejected) {
      return static_cast<DialogResult>(result);
    }
    // closed with Esc or the title bar: Cancel when there is one, as Windows
    // does; otherwise the answer that changes nothing
    if (group.button(cancelId) != nullptr) {
      return DialogResult::Cancel;
    }
    if (group.button(noId) != nullptr) {
      return DialogResult::No;
    }
    if (group.button(ignoreId) != nullptr) {
      return DialogResult::Ignore;
    }
    return DialogResult::OK;
  }


  array<int>^ BaseScriptImpl::Select(array<SelectOption^>^ p_sopOptions, String^ p_strTitle, bool p_booSelectMany) {
    using namespace System::Collections::Generic;

    const QString title = String::IsNullOrEmpty(p_strTitle)
                              ? (p_booSelectMany ? scriptText("Choose any of these")
                                                 : scriptText("Choose one"))
                              : to_qstring(p_strTitle);
    RevampedFluent::ContentDialog dialog(
        g.ParentWidget, QStringLiteral("csharpScriptSelect"), title,
        p_booSelectMany ? scriptText("Check the options you want.")
                        : scriptText("Select the option you want."));
    dialog.setMinimumSize(720, 460);
    dialog.setSizeGripEnabled(true);

    // the options on the left; the selected one's picture and description on
    // the right, one page per option
    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    auto* list = new QListWidget(&dialog);
    list->setAccessibleName(title);
    list->setMinimumWidth(240);
    auto* pages = new QStackedWidget(&dialog);
    pages->setProperty("fluentCard", true);

    for each (SelectOption ^ opt in p_sopOptions) {
      const QString name = to_qstring(opt->Item);
      auto* item = new QListWidgetItem(name, list);
      if (p_booSelectMany) {
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
      }

      auto* page = new QWidget(pages);
      auto* pageLayout = new QVBoxLayout(page);
      pageLayout->setContentsMargins(16, 14, 16, 14);
      pageLayout->setSpacing(10);
      if (!String::IsNullOrEmpty(opt->Preview)) {
        QPixmap picture;
        if (auto entry = g.SourceTree->find(to_qstring(opt->Preview)); entry != nullptr) {
          const QString path = extractFile(entry);
          if (!path.isEmpty()) {
            picture.load(path);
          }
        }
        if (!picture.isNull()) {
          auto* label = new QLabel(page);
          label->setAlignment(Qt::AlignCenter);
          label->setPixmap(picture.scaled(QSize(420, 240), Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation));
          label->setAccessibleName(scriptText("Picture of the option"));
          pageLayout->addWidget(label);
        }
      }
      auto* nameLabel = new QLabel(name, page);
      nameLabel->setObjectName(QStringLiteral("instanceDetailName"));
      nameLabel->setTextFormat(Qt::PlainText);
      nameLabel->setWordWrap(true);
      pageLayout->addWidget(nameLabel);
      auto* description = new QPlainTextEdit(page);
      description->setReadOnly(true);
      description->setFrameShape(QFrame::NoFrame);
      // text on the card, not a field: nothing here can be typed into
      description->setStyleSheet(QStringLiteral("QPlainTextEdit { background: transparent; border: none; padding: 0px; }"));
      description->document()->setDocumentMargin(0);
      description->setPlainText(to_qstring(opt->Desc));
      description->setAccessibleName(scriptText("Option description"));
      pageLayout->addWidget(description, 1);
      pages->addWidget(page);
    }

    row->addWidget(list, 2);
    row->addWidget(pages, 3);
    dialog.body()->addLayout(row, 1);

    QObject::connect(list, SIGNAL(currentRowChanged(int)), pages, SLOT(setCurrentIndex(int)));
    if (p_booSelectMany) {
      list->viewport()->installEventFilter(new SelectNameClicks(list));
    }
    if (list->count() > 0) {
      list->setCurrentRow(0);
    }

    QPushButton* ok     = dialog.addButton(scriptText("OK"), true);
    QPushButton* cancel = dialog.addButton(scriptText("Cancel"), false);
    QObject::connect(ok, SIGNAL(clicked()), &dialog, SLOT(accept()));
    QObject::connect(cancel, SIGNAL(clicked()), &dialog, SLOT(reject()));
    list->setFocus();

    if (dialog.exec() != QDialog::Accepted) {
      return gcnew array<int>(0);
    }

    List<int>^ selected = gcnew List<int>(list->count());
    for (int i = 0; i < list->count(); ++i) {
      const bool chosen = p_booSelectMany ? list->item(i)->checkState() == Qt::Checked
                                          : i == list->currentRow();
      if (chosen) {
        selected->Add(i);
      }
    }

    return selected->ToArray();
  }

  // Versioning / INIs:

  // Convert to Version^ from a MO2 version:
  inline System::Version^ make_version(VersionInfo version) {
    auto qversion = version.asQVersionNumber();
    return gcnew System::Version(qversion.majorVersion(), qversion.minorVersion(), qversion.microVersion());

  }

  // MO2 Revamped: a script's own window opens centred over MO2's window in
  // front (the installer's), and stays above it, instead of at Windows'
  // default spot on the primary monitor with no owner
  ref class CustomFormPlacement {
  public:
    CustomFormPlacement(IntPtr owner) : m_owner(owner) {}

    void OnLoad(Object^ sender, EventArgs^) {
      Form^ form = safe_cast<Form^>(sender);
      HWND owner = static_cast<HWND>(m_owner.ToPointer());
      RECT r;
      if (!IsWindow(owner) || !GetWindowRect(owner, &r)) {
        return;
      }
      // ShowDialog() gives it the active window as owner; Show() gives none
      if (!form->Modal) {
        SetWindowLongPtrW(static_cast<HWND>(form->Handle.ToPointer()), GWLP_HWNDPARENT,
                          reinterpret_cast<LONG_PTR>(owner));
      }
      // WinForms works in physical pixels, as GetWindowRect does
      System::Drawing::Rectangle area = Screen::FromHandle(m_owner)->WorkingArea;
      int x = (r.left + r.right) / 2 - form->Width / 2;
      int y = (r.top + r.bottom) / 2 - form->Height / 2;
      x = Math::Max(area.Left, Math::Min(x, area.Right - form->Width));
      y = Math::Max(area.Top, Math::Min(y, area.Bottom - form->Height));
      form->StartPosition = FormStartPosition::Manual;
      form->Location = System::Drawing::Point(x, y);
    }

  private:
    IntPtr m_owner;
  };

  Form^ BaseScriptImpl::CreateCustomForm() {
    Form^ form = gcnew Form;
    QWidget* owner = QApplication::activeModalWidget() != nullptr
                         ? QApplication::activeModalWidget()->window()
                         : (g.ParentWidget != nullptr ? g.ParentWidget->window() : nullptr);
    if (owner != nullptr) {
      CustomFormPlacement^ placement =
          gcnew CustomFormPlacement(IntPtr(reinterpret_cast<void*>(owner->winId())));
      form->Load += gcnew EventHandler(placement, &CustomFormPlacement::OnLoad);
    }
    return form;
  }

  System::Version^ BaseScriptImpl::GetModManagerVersion() {
    return make_version(g_Organizer->appVersion());
  }

  System::Version^ BaseScriptImpl::GetGameVersion() {
    return make_version(g_Organizer->managedGame()->gameVersion());
  }

  System::Version^ BaseScriptImpl::GetScriptExtenderVersion() {
    auto scriptExtender = g_Organizer->gameFeatures()->gameFeature<ScriptExtender>();

    if (!scriptExtender || !scriptExtender->isInstalled()) {
      return nullptr;
    }

    return gcnew System::Version(msclr::interop::marshal_as<String^>(scriptExtender->getExtenderVersion().toStdString()));
  }

  bool BaseScriptImpl::ScriptExtenderPresent() {
    auto scriptExtender = g_Organizer->gameFeatures()->gameFeature<ScriptExtender>();
    return scriptExtender && scriptExtender->isInstalled();
  }

  // Plugins:
  array<String^>^ BaseScriptImpl::GetAllPlugins() {
    QStringList names = g_Organizer->pluginList()->pluginNames();
    array<String^>^ result = gcnew array<String^>(names.size());
    for (int i = 0; i < names.size(); ++i) {
      result[i] = from_string(names[i].toStdString());
    }
    return result;
  }

  array<String^>^ BaseScriptImpl::GetActivePlugins() {
    auto pluginList = g_Organizer->pluginList();
    QStringList names = pluginList->pluginNames();
    QStringList activeNames;
    for (auto& name : names) {
      if (pluginList->state(name) == IPluginList::STATE_ACTIVE) {
        activeNames.append(name);
      }
    }
    array<String^>^ result = gcnew array<String^>(activeNames.size());
    for (int i = 0; i < activeNames.size(); ++i) {
      result[i] = from_string(activeNames[i].toStdString());
    }
    return result;
  }

  // INIs:
  String^ BaseScriptImpl::GetIniString(String^ settingsFileName, String^ section, String^ key) {

    // Check if we have already set this within this installation:
    auto fIt = g.Settings.find(to_qstring(settingsFileName));
    if (fIt != g.Settings.end()) {
      QString value = fIt->second.value(to_qstring(section), to_qstring(key));
      if (!value.isEmpty()) {
        return from_string(value);
      }
    }

    // Otherwize, look-up the file:
    QDir path(g_Organizer->profilePath());
    if (!g_Organizer->profile()->localSettingsEnabled()) {
      path = QDir(g_Organizer->managedGame()->documentsDirectory());
    }

    QSettings settings(path.filePath(to_qstring(settingsFileName)), QSettings::IniFormat);

    if (settings.status() != QSettings::NoError) {
      return nullptr;
    }

    QString name = to_qstring(section + "/" + key);
    if (section->Equals("General", System::StringComparison::CurrentCultureIgnoreCase)) {
      name = to_qstring(key);
    }

    QVariant value = settings.value(name);
    if (!value.isValid()) {
      return nullptr;
    }

    return from_string(value.toString().toStdString());
  }

  int BaseScriptImpl::GetIniInt(String^ settingsFileName, String^ section, String^ key) {
    return Convert::ToInt32(GetIniString(settingsFileName, section, key));
  }

  bool BaseScriptImpl::EditIni(String^ p_strSettingsFileName, String^ p_strSection, String^ p_strKey, String^ p_strValue) {
    // Check that the file is supported:
    bool iniFound = false;
    for (auto ini : g_Organizer->managedGame()->iniFiles()) {
      if (ini.compare(to_qstring(p_strSettingsFileName), Qt::CaseInsensitive) == 0) {
        iniFound = true;
      }
    }

    if (!iniFound) {
      return false;
    }

    g.Settings[to_qstring(p_strSettingsFileName)].setValue(to_qstring(p_strSection), to_qstring(p_strKey), to_qstring(p_strValue));
    return true;
  }

}
