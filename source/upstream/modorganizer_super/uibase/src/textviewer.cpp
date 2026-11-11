/*
Mod Organizer shared UI functionality

Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "textviewer.h"
#include "finddialog.h"
#include "log.h"
#include "report.h"
#include "utility.h"
#include <QAction>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcutEvent>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace
{
// Enter on a focused button presses that button, as in Windows
class EnterPressesItself : public QObject
{
public:
  using QObject::QObject;

  bool eventFilter(QObject* watched, QEvent* event) override
  {
    auto* button = qobject_cast<QPushButton*>(watched);
    if (event->type() == QEvent::KeyPress && button != nullptr && button->isEnabled()) {
      const int key = static_cast<QKeyEvent*>(event)->key();
      if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        button->click();
        return true;
      }
    }
    return false;
  }
};
}  // namespace

// MO2 Revamped: built in code instead of from 2.5.2's .ui; the header, and so
// uibase's exports, stay as they were
namespace Ui
{
class TextViewer
{
public:
  QLabel* title;
  QLabel* descriptionLabel;
  QCheckBox* showWhitespace;
  QTabWidget* editorTabs;
  QPushButton* findBtn;
  QPushButton* closeBtn;

  void setupUi(QDialog* dialog)
  {
    dialog->setObjectName(QStringLiteral("TextViewer"));
    dialog->setWindowFlags(dialog->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog->setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    dialog->setSizeGripEnabled(true);
    dialog->setMinimumSize(560, 400);
    dialog->resize(820, 600);

    auto* root = new QVBoxLayout(dialog);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(8);
    title = new QLabel(dialog);
    title->setProperty("wizardPageTitle", true);
    title->setTextFormat(Qt::PlainText);
    root->addWidget(title);
    descriptionLabel = new QLabel(dialog);
    descriptionLabel->setObjectName(QStringLiteral("descriptionLabel"));
    descriptionLabel->setProperty("wizardPageText", true);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setVisible(false);
    root->addWidget(descriptionLabel);
    editorTabs = new QTabWidget(dialog);
    editorTabs->setObjectName(QStringLiteral("editorTabs"));
    editorTabs->setDocumentMode(true);
    root->addWidget(editorTabs, 1);

    auto* footer = new QHBoxLayout;
    footer->setSpacing(8);
    showWhitespace = new QCheckBox(
        QCoreApplication::translate("TextViewer", "Show spaces and tabs"), dialog);
    showWhitespace->setObjectName(QStringLiteral("showWhitespace"));
    // on, as in 2.5.2's .ui
    showWhitespace->setChecked(true);
    footer->addWidget(showWhitespace);
    footer->addStretch(1);
    findBtn = new QPushButton(QCoreApplication::translate("TextViewer", "Find…"), dialog);
    findBtn->setObjectName(QStringLiteral("findBtn"));
    findBtn->setToolTip(QCoreApplication::translate("TextViewer", "Find in this file (Ctrl+F)"));
    findBtn->setAutoDefault(false);
    closeBtn = new QPushButton(QCoreApplication::translate("TextViewer", "Close"), dialog);
    closeBtn->setObjectName(QStringLiteral("closeBtn"));
    closeBtn->setAutoDefault(false);
    for (auto* button : {findBtn, closeBtn}) {
      button->setMinimumWidth(110);
      button->installEventFilter(new EnterPressesItself(button));
      footer->addWidget(button);
    }
    root->addLayout(footer);
  }
};
}  // namespace Ui

namespace MOBase
{

TextViewer::TextViewer(const QString& title, QWidget* parent)
    : QDialog(parent), ui(new Ui::TextViewer), m_FindDialog(nullptr)
{
  ui->setupUi(this);
  setWindowTitle(title);
  ui->title->setText(title);
  m_EditorTabs = ui->editorTabs;
  connect(ui->showWhitespace, SIGNAL(stateChanged(int)), this,
          SLOT(showWhitespaceChanged(int)));
  connect(ui->findBtn, &QPushButton::clicked, this, [this] {
    find();
  });
  connect(ui->closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

TextViewer::~TextViewer()
{
  delete ui;
}

void TextViewer::closeEvent(QCloseEvent* event)
{
  if (!m_Modified.empty()) {
    for (std::set<QTextEdit*>::iterator iter = m_Modified.begin();
         iter != m_Modified.end(); ++iter) {
      QMessageBox::StandardButton res = QMessageBox::question(
          this, tr("Save changes?"),
          tr("Do you want to save changes to %1?").arg((*iter)->documentTitle()),
          QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
      if (res == QMessageBox::Yes) {
        saveFile(*iter);
      } else if (res == QMessageBox::Cancel) {
        event->ignore();
        break;
      }
    }
  }
}

void TextViewer::find()
{
  if (!m_FindDialog) {
    m_FindDialog = new FindDialog(this);
    connect(m_FindDialog, SIGNAL(findNext()), this, SLOT(findNext()));
    connect(m_FindDialog, SIGNAL(patternChanged(QString)), this,
            SLOT(patternChanged(QString)));
  }

  m_FindDialog->show();
  m_FindDialog->raise();
  m_FindDialog->activateWindow();
}

void TextViewer::patternChanged(QString newPattern)
{
  m_FindPattern = newPattern;
}

void TextViewer::findNext()
{
  if (m_FindPattern.length() == 0) {
    return;
  }

  QWidget* currentPage = m_EditorTabs->currentWidget();
  // the viewer can open with no file at all
  QTextEdit* editor =
      currentPage != nullptr ? currentPage->findChild<QTextEdit*>("editorView") : nullptr;
  if (editor == nullptr) {
    return;
  }

  if (editor->find(m_FindPattern)) {
    // found text
    return;
  } else {
    // reached the bottom and no text found,
    // we wrap around once.

    // save current cursor
    auto oldCursor = editor->textCursor();

    editor->moveCursor(QTextCursor::Start);

    // search again from the top
    if (editor->find(m_FindPattern)) {
      // found something, keep new cursor position.
      return;
    } else {
      // there are no matches in the document,
      // restore previous cursor.
      editor->setTextCursor(oldCursor);
    }
  }
}

void TextViewer::showWhitespaceChanged(int state)
{
  for (int i = 0; i < m_EditorTabs->count(); ++i) {
    QTextEdit* editor = m_EditorTabs->widget(i)->findChild<QTextEdit*>();
    if (editor != nullptr) {
      auto document   = editor->document();
      auto textOption = document->defaultTextOption();
      auto flags      = textOption.flags();
      if (state == Qt::Unchecked)
        flags = flags & (~QTextOption::ShowTabsAndSpaces);
      else
        flags = flags | QTextOption::ShowTabsAndSpaces;
      textOption.setFlags(flags);
      document->setDefaultTextOption(textOption);
      editor->setDocument(document);
    }
  }
}

bool TextViewer::eventFilter(QObject* object, QEvent* event)
{
  if (event->type() == QEvent::ShortcutOverride) {
    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->matches(QKeySequence::Find)) {
      find();
    } else if (keyEvent->matches(QKeySequence::FindNext)) {
      findNext();
    }
  }
  return QDialog::eventFilter(object, event);
}

void TextViewer::setDescription(const QString& description)
{
  ui->descriptionLabel->setText(description);
  ui->descriptionLabel->setVisible(!description.isEmpty());
}

void TextViewer::saveFile(const QTextEdit* editor)
{
  bool write                                = true;
  QMessageBox::StandardButton buttonPressed = QMessageBox::Ignore;
  QFile file(editor->documentTitle());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    write = false;
    QFileInfo fileInfo(file.fileName());
    buttonPressed =
        MOBase::TaskDialog(qApp->activeModalWidget(),
                           QObject::tr("INI file is read-only"))
            .main(QObject::tr("INI file is read-only"))
            .content(QObject::tr("Mod Organizer is attempting to write to \"%1\" which "
                                 "is currently set to read-only.")
                         .arg(fileInfo.fileName()))
            .icon(QMessageBox::Warning)
            .button({QObject::tr("Clear the read-only flag"), QMessageBox::Yes})
            .button({QObject::tr("Allow the write once"),
                     QObject::tr("The file will be set to read-only again."),
                     QMessageBox::Ignore})
            .button({QObject::tr("Skip this file"), QMessageBox::No})
            .remember("clearReadOnly", fileInfo.fileName())
            .exec();

    if (buttonPressed & (QMessageBox::Yes | QMessageBox::Ignore)) {
      file.setPermissions(file.permissions() | QFile::WriteUser);
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      reportError(tr("failed to write to %1").arg(editor->documentTitle()));
    } else {
      write = true;
    }
  }

  if (write) {
    file.write(editor->toPlainText().toUtf8().replace('\n', "\r\n"));
    file.close();
  }

  if (buttonPressed == QMessageBox::Ignore) {
    file.setPermissions(file.permissions() & ~(QFile::WriteUser));
  }
}

void TextViewer::saveFile()
{
  QWidget* currentPage = m_EditorTabs->currentWidget();
  QTextEdit* editor    = currentPage->findChild<QTextEdit*>("editorView");
  saveFile(editor);

  m_Modified.erase(editor);
}

void TextViewer::modified()
{
  QWidget* currentPage = m_EditorTabs->currentWidget();
  QTextEdit* editor    = currentPage->findChild<QTextEdit*>("editorView");

  m_Modified.insert(editor);
}

void TextViewer::addFile(const QString& fileName, bool writable)
{
  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly)) {
    throw Exception(tr("file not found: %1").arg(fileName));
  }
  QByteArray temp = file.readAll();

  QWidget* page           = new QWidget();
  QVBoxLayout* layout     = new QVBoxLayout(page);
  layout->setContentsMargins(0, 8, 0, 0);
  layout->setSpacing(8);
  QTextEdit* editor       = new QTextEdit(page);
  QTextDocument* document = new QTextDocument(page);
  if (ui->showWhitespace->isChecked()) {
    QTextOption option;
    option.setFlags(QTextOption::ShowTabsAndSpaces);
    document->setDefaultTextOption(option);
  }
  editor->setDocument(document);
  editor->setAcceptRichText(false);
  editor->setPlainText(QString(temp));
  editor->setLineWrapMode(QTextEdit::NoWrap);
  editor->setObjectName("editorView");
  editor->setDocumentTitle(fileName);
  editor->installEventFilter(this);
  editor->setReadOnly(!writable);

  // set text highlighting color in inactive window equal to text hightlighting color in
  // active window
  QPalette palette = editor->palette();
  palette.setColor(QPalette::Inactive, QPalette::Highlight,
                   palette.color(QPalette::Active, QPalette::Highlight));
  palette.setColor(QPalette::Inactive, QPalette::HighlightedText,
                   palette.color(QPalette::Active, QPalette::HighlightedText));
  editor->setPalette(palette);

  // add hotkeys for searching through the document
  QAction* findAction = new QAction(QString("&Find"), editor);
  findAction->setShortcut(QKeySequence::Find);
  editor->addAction(findAction);
  QAction* findNextAction = new QAction(QString("Find &Next"), editor);
  // 2.5.2 set this on findAction, replacing Ctrl+F with F3
  findNextAction->setShortcut(QKeySequence::FindNext);
  editor->addAction(findNextAction);

  editor->setAccessibleName(QFileInfo(fileName).fileName());
  editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  layout->addWidget(editor);
  if (writable) {
    // the file's Save, at the right of its page
    auto* row            = new QHBoxLayout;
    QPushButton* saveBtn =
        new QPushButton(QCoreApplication::translate("TextViewer", "Save"), page);
    saveBtn->installEventFilter(new EnterPressesItself(saveBtn));
    saveBtn->setProperty("fluentPrimary", true);
    saveBtn->setAutoDefault(false);
    saveBtn->setMinimumWidth(110);
    saveBtn->setToolTip(tr("Save %1").arg(QFileInfo(fileName).fileName()));
    row->addStretch(1);
    row->addWidget(saveBtn);
    layout->addLayout(row);
    connect(saveBtn, SIGNAL(clicked()), this, SLOT(saveFile()));
    connect(editor, SIGNAL(textChanged()), this, SLOT(modified()));
  }
  page->setLayout(layout);
  m_EditorTabs->addTab(page, QFileInfo(fileName).fileName());
}
}  // namespace MOBase
