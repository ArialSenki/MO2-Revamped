#include "rtfPopup.h"

#include <QEvent>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QTextDocument>

#include "../interop/QtDotNetConverters.h"

RtfPopup::RtfPopup(System::String^ rtfText, QWidget* parent, Qt::WindowFlags f)
    : RevampedFluent::ContentDialog(parent, QStringLiteral("omodReadme"), tr("Readme"))
{
  if (f != Qt::WindowFlags()) {
    setWindowFlags(windowFlags() | f);
  }
  QString text = rtfText->StartsWith("{\\rtf") ? toQString(RtfPipe::Rtf::ToHtml(rtfText)) : Qt::convertFromPlainText(toQString(rtfText), Qt::WhiteSpaceNormal);
  QRegularExpression urlFinder(R"REGEX((?<!(?:href="))((?:(?:https?|ftp|file)://|www\.|ftp\.)(?:\([-A-Z0-9+@#/%=~_|$?!:,.]|(?:&amp;)*\)|[-A-Z0-9+@#/%=~_|$?!:,.]|(?:&amp;))*(?:\([-A-Z0-9+@#/%=~+|$?!:,.]|(?:&amp;)*\)|[A-Z0-9+@#/%=~_|$]|(?:&amp;))))REGEX", QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
  text.replace(urlFinder, R"(<a href="\1">\1</a>)");

  setMinimumSize(460, 320);
  resize(560, 480);
  setSizeGripEnabled(true);

  // a text browser scrolls and selects as the text needs; 2.5.2 put a label
  // in a scroll area
  auto* browser = new QTextBrowser(this);
  browser->setOpenExternalLinks(true);
  browser->setHtml(text);
  browser->setAccessibleName(tr("Readme"));
  body()->addWidget(browser, 1);

  auto* close = addButton(tr("Close"), true);
  // a string connect: C++/CLI finds no signal for a function pointer
  connect(close, SIGNAL(clicked()), this, SLOT(accept()));
}

void RtfPopup::changeEvent(QEvent* event)
{
  if (event->type() == QEvent::WindowTitleChange) {
    setHeading(windowTitle());
  }
  RevampedFluent::ContentDialog::changeEvent(event);
}
