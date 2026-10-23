#include "handlerwindow.h"

#include "fluentstyle.h"
#include "handlereditdialog.h"
#include "installinfo.h"
#include "linktestdialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{

enum Roles
{
  GamesRole = Qt::UserRole + 1,
  ExecutableRole,
  ArgumentsRole
};

// Segoe Fluent Icons / Segoe MDL2 Assets code points
constexpr ushort GlyphAdd      = 0xE710;
constexpr ushort GlyphEdit     = 0xE70F;
constexpr ushort GlyphDelete   = 0xE74D;
constexpr ushort GlyphUp       = 0xE74A;
constexpr ushort GlyphDown     = 0xE74B;
constexpr ushort GlyphAll      = 0xE8FD;
constexpr ushort GlyphTest     = 0xE768;
constexpr ushort GlyphWarning  = 0xE7BA;
constexpr ushort GlyphLink     = 0xE71B;

enum class EntryStatus
{
  Ready,
  Missing,
  Registered
};

bool isNxmHandler(const QString& path)
{
  return path.endsWith(QLatin1String("nxmhandler.exe"), Qt::CaseInsensitive);
}

// "Registered": Windows sends links to this very program, or to the
// nxmhandler.exe in its folder (the Mod Organizer the program belongs to)
EntryStatus entryStatus(const QString& executable, const QString& registered)
{
  const QFileInfo file(executable);
  if (executable.isEmpty() || !file.isFile()) {
    return EntryStatus::Missing;
  }
  if (samePath(executable, registered) ||
      (isNxmHandler(registered) &&
       samePath(QFileInfo(registered).absolutePath(), file.absolutePath()))) {
    return EntryStatus::Registered;
  }
  return EntryStatus::Ready;
}

QString statusText(EntryStatus status)
{
  switch (status) {
  case EntryStatus::Missing:
    return HandlerWindow::tr("Missing");
  case EntryStatus::Registered:
    return HandlerWindow::tr("Registered");
  case EntryStatus::Ready:
    break;
  }
  return HandlerWindow::tr("Ready");
}

QString statusToolTip(EntryStatus status)
{
  switch (status) {
  case EntryStatus::Missing:
    return HandlerWindow::tr("This program no longer exists. Links for its games cannot be "
                             "opened until you remove or fix it.");
  case EntryStatus::Registered:
    return HandlerWindow::tr("Windows sends Nexus links to the nxmhandler in this program's "
                             "folder, which passes them on using this list.");
  case EntryStatus::Ready:
    break;
  }
  return HandlerWindow::tr("This program can receive links for its games.");
}

QString entryTitle(const InstallInfo& info)
{
  QString title = info.title();
  if (!info.layoutText().isEmpty()) {
    title += QStringLiteral("  ·  ") + info.layoutText();
  }
  return title;
}

// the handler list: rows drawn by HandlerDelegate, ordered by drag and drop,
// with a short hint when it is empty
}  // namespace

class HandlerList : public QListWidget
{
public:
  using QListWidget::QListWidget;

protected:
  void paintEvent(QPaintEvent* event) override
  {
    QListWidget::paintEvent(event);
    if (count() > 0) {
      return;
    }
    QPainter painter(viewport());
    painter.setPen(Fluent::theme().caption);
    painter.drawText(viewport()->rect().adjusted(24, 0, -24, 0),
                     Qt::AlignCenter | Qt::TextWordWrap,
                     HandlerWindow::tr("No programs yet. Add the Mod Organizer that should open "
                                       "Nexus links."));
  }
};

namespace
{

class HandlerDelegate : public QStyledItemDelegate
{
public:
  HandlerDelegate(HandlerWindow* window) : QStyledItemDelegate(window), m_Window(window) {}

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
  {
    return QSize(option.rect.width(), 90);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    const Fluent::Theme& t = Fluent::theme();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRectF row = QRectF(option.rect).adjusted(2, 2, -2, -2);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered  = option.state & QStyle::State_MouseOver;

    // row background and the Fluent selection pill
    if (selected || hovered) {
      QPainterPath path;
      path.addRoundedRect(row, 6, 6);
      painter->fillPath(path, selected ? t.sel : t.hover);
    }
    if (selected) {
      QPainterPath pill;
      pill.addRoundedRect(QRectF(row.left() + 1, row.center().y() - 9, 3, 18), 1.5, 1.5);
      painter->fillPath(pill, t.accent);
    }

    const QString executable = index.data(ExecutableRole).toString();
    const QString arguments  = index.data(ArgumentsRole).toString();
    const QStringList games  = index.data(GamesRole).toStringList();
    const InstallInfo info   = InstallInfo::describe(executable);
    const EntryStatus status = entryStatus(executable, m_Window->primaryHandler());

    // priority: the first program that handles a game gets its links
    const QRectF badge(row.left() + 14, row.top() + 12, 26, 26);
    painter->setPen(QPen(t.cardLine.darker(t.dark ? 70 : 115), 1));
    painter->setBrush(t.card);
    painter->drawEllipse(badge);
    QFont font = option.font;
    QFont badgeFont = font;
    badgeFont.setWeight(QFont::DemiBold);
    painter->setFont(badgeFont);
    painter->setPen(t.muted);
    painter->drawText(badge, Qt::AlignCenter, QString::number(index.row() + 1));

    const qreal left  = badge.right() + 14;
    qreal right       = row.right() - 14;

    // status pill
    QFont pillFont = font;
    pillFont.setPointSizeF(font.pointSizeF() * 0.92);
    pillFont.setWeight(QFont::DemiBold);
    const QString pillText = statusText(status);
    const QFontMetricsF pillMetrics(pillFont);
    const qreal pillWidth = pillMetrics.horizontalAdvance(pillText) + 20;
    const QRectF pill(right - pillWidth, row.top() + 12, pillWidth, 22);
    QColor pillBg = t.okBg, pillFg = t.okFg;
    if (status == EntryStatus::Missing) {
      pillBg = t.critBg;
      pillFg = t.critFg;
    } else if (status == EntryStatus::Registered) {
      pillBg = t.accentSubtle;
      pillFg = t.accentText;
    }
    QPainterPath pillPath;
    pillPath.addRoundedRect(pill, 11, 11);
    painter->fillPath(pillPath, pillBg);
    painter->setFont(pillFont);
    painter->setPen(pillFg);
    painter->drawText(pill, Qt::AlignCenter, pillText);
    right = pill.left() - 12;

    // title: product, version and kind of install
    QFont titleFont = font;
    titleFont.setWeight(QFont::DemiBold);
    titleFont.setPointSizeF(font.pointSizeF() * 1.08);
    painter->setFont(titleFont);
    painter->setPen(status == EntryStatus::Missing ? t.muted : t.text);
    const QRectF titleRect(left, row.top() + 10, right - left, 22);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                      QFontMetricsF(titleFont).elidedText(entryTitle(info), Qt::ElideRight,
                                                          titleRect.width()));

    // second line: folder (or the missing file) and the arguments
    QString detail = info.exists ? info.folder : info.executable;
    if (!arguments.trimmed().isEmpty()) {
      detail += QStringLiteral("   ·   ") + HandlerWindow::tr("Arguments: %1").arg(arguments);
    }
    QFont captionFont = font;
    captionFont.setPointSizeF(font.pointSizeF() * 0.92);
    painter->setFont(captionFont);
    painter->setPen(t.caption);
    const QRectF detailRect(left, titleRect.bottom() + 1, row.right() - 14 - left, 18);
    painter->drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                      QFontMetricsF(captionFont).elidedText(detail, Qt::ElideMiddle,
                                                            detailRect.width()));

    // third line: one chip per game
    const QFontMetricsF chipMetrics(captionFont);
    qreal x            = left;
    const qreal y      = detailRect.bottom() + 6;
    const qreal limit  = row.right() - 14;
    const qreal height = 22;
    if (games.isEmpty()) {
      painter->setPen(t.caption);
      painter->drawText(QRectF(x, y, limit - x, height), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                        HandlerWindow::tr("No games"));
    }
    for (int i = 0; i < games.size(); ++i) {
      const QString name  = gameDisplayName(games.at(i));
      const qreal width   = chipMetrics.horizontalAdvance(name) + 18;
      const QString more  = QStringLiteral("+%1").arg(games.size() - i);
      const qreal moreW   = chipMetrics.horizontalAdvance(more) + 18;
      const bool last     = i == games.size() - 1;
      if (x + width > limit || (!last && x + width + 6 + moreW > limit)) {
        drawChip(painter, QRectF(x, y, moreW, height), more, t, captionFont);
        break;
      }
      drawChip(painter, QRectF(x, y, width, height), name, t, captionFont);
      x += width + 6;
    }

    painter->restore();
  }

private:
  static void drawChip(QPainter* painter, const QRectF& rect, const QString& text,
                       const Fluent::Theme& t, const QFont& font)
  {
    QPainterPath path;
    path.addRoundedRect(rect, 4, 4);
    painter->fillPath(path, t.chip);
    painter->setFont(font);
    painter->setPen(t.chipText);
    painter->drawText(rect, Qt::AlignCenter, text);
  }

  HandlerWindow* m_Window;
};

QToolButton* commandButton(const QString& text, QWidget* parent, bool showText = true)
{
  auto* button = new QToolButton(parent);
  button->setText(text);
  button->setToolTip(text);
  button->setToolButtonStyle(showText ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
  button->setAutoRaise(true);
  button->setIconSize(QSize(16, 16));
  return button;
}

QLabel* glyphLabel(QWidget* parent)
{
  auto* label = new QLabel(parent);
  label->setFixedSize(20, 20);
  return label;
}

}  // namespace

HandlerWindow::HandlerWindow(QWidget* parent) : QMainWindow(parent)
{
  setObjectName(QStringLiteral("HandlerWindow"));
  setWindowTitle(tr("NXM Handler"));
  resize(780, 720);
  setMinimumSize(560, 480);

  auto* page = new QWidget(this);
  page->setObjectName(QStringLiteral("page"));
  page->setAttribute(Qt::WA_StyledBackground, true);
  setCentralWidget(page);
  auto* pageLayout = new QVBoxLayout(page);
  pageLayout->setContentsMargins(0, 0, 0, 0);
  pageLayout->setSpacing(0);

  auto* content = new QWidget(page);
  auto* layout  = new QVBoxLayout(content);
  layout->setContentsMargins(24, 20, 24, 20);
  layout->setSpacing(12);

  // header
  auto* title = new QLabel(tr("How to handle Nexus links"), content);
  title->setObjectName(QStringLiteral("pageTitle"));
  auto* subtitle = new QLabel(tr("Choose which program opens Nexus Mods download links for "
                                 "each game. When several programs handle a game, the one "
                                 "highest in the list gets the link."),
                              content);
  subtitle->setObjectName(QStringLiteral("pageSubtitle"));
  subtitle->setWordWrap(true);
  auto* header = new QVBoxLayout;
  header->setSpacing(4);
  header->addWidget(title);
  header->addWidget(subtitle);
  layout->addLayout(header);
  layout->addSpacing(4);

  // registration card: which program Windows sends nxm links to
  auto* registration = new QFrame(content);
  Fluent::makeCard(registration);
  auto* registrationLayout = new QHBoxLayout(registration);
  registrationLayout->setContentsMargins(16, 14, 16, 14);
  registrationLayout->setSpacing(12);
  m_RegistrationIcon = glyphLabel(registration);
  registrationLayout->addWidget(m_RegistrationIcon, 0, Qt::AlignTop);
  auto* registrationText = new QVBoxLayout;
  registrationText->setSpacing(2);
  auto* registrationCaption = new QLabel(tr("Windows sends Nexus links to"), registration);
  registrationCaption->setProperty("caption", true);
  m_RegistrationValue = new QLabel(registration);
  m_RegistrationValue->setObjectName(QStringLiteral("registrationValue"));
  m_RegistrationValue->setWordWrap(true);
  m_RegistrationPath = new QLabel(registration);
  m_RegistrationPath->setProperty("caption", true);
  m_RegistrationPath->setWordWrap(true);
  m_RegistrationPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
  registrationText->addWidget(registrationCaption);
  registrationText->addWidget(m_RegistrationValue);
  registrationText->addWidget(m_RegistrationPath);
  registrationLayout->addLayout(registrationText, 1);
  m_RegistrationStatus = new QLabel(registration);
  m_RegistrationStatus->setObjectName(QStringLiteral("statusPill"));
  registrationLayout->addWidget(m_RegistrationStatus, 0, Qt::AlignVCenter);
  m_RegisterButton = new QPushButton(tr("Register this installation"), registration);
  m_RegisterButton->setAutoDefault(false);
  m_RegisterButton->setToolTip(
      tr("Makes Windows send Nexus links to this installation's nxmhandler, which passes "
         "them on using the list below."));
  registrationLayout->addWidget(m_RegisterButton, 0, Qt::AlignVCenter);
  layout->addWidget(registration);

  // missing programs
  m_MissingBar = new QFrame(content);
  m_MissingBar->setObjectName(QStringLiteral("infoBar"));
  m_MissingBar->setProperty("kind", QStringLiteral("warn"));
  m_MissingBar->setAttribute(Qt::WA_StyledBackground, true);
  auto* missingLayout = new QHBoxLayout(m_MissingBar);
  missingLayout->setContentsMargins(14, 8, 10, 8);
  missingLayout->setSpacing(10);
  m_MissingIcon = glyphLabel(m_MissingBar);
  missingLayout->addWidget(m_MissingIcon);
  m_MissingText = new QLabel(m_MissingBar);
  m_MissingText->setWordWrap(true);
  missingLayout->addWidget(m_MissingText, 1);
  auto* removeMissingButton = new QPushButton(tr("Remove missing"), m_MissingBar);
  removeMissingButton->setAutoDefault(false);
  missingLayout->addWidget(removeMissingButton);
  layout->addWidget(m_MissingBar);

  // the handler list
  auto* listCard = new QFrame(content);
  Fluent::makeCard(listCard);
  auto* listLayout = new QVBoxLayout(listCard);
  listLayout->setContentsMargins(8, 10, 8, 8);
  listLayout->setSpacing(6);

  auto* listHeader = new QHBoxLayout;
  listHeader->setContentsMargins(8, 0, 0, 0);
  listHeader->setSpacing(4);
  auto* listTitles = new QVBoxLayout;
  listTitles->setSpacing(0);
  auto* listTitle = new QLabel(tr("Programs"), listCard);
  listTitle->setObjectName(QStringLiteral("cardTitle"));
  auto* listHint = new QLabel(tr("Drag a program up or down to change which one gets the link."),
                              listCard);
  listHint->setProperty("caption", true);
  listTitles->addWidget(listTitle);
  listTitles->addWidget(listHint);
  listHeader->addLayout(listTitles, 1);
  m_TestButton = commandButton(tr("Test a link..."), listCard);
  m_AddButton  = commandButton(tr("Add program..."), listCard);
  listHeader->addWidget(m_TestButton);
  listHeader->addWidget(m_AddButton);
  listLayout->addLayout(listHeader);

  m_List = new HandlerList(listCard);
  m_List->setObjectName(QStringLiteral("handlerList"));
  m_List->setItemDelegate(new HandlerDelegate(this));
  m_List->setContextMenuPolicy(Qt::CustomContextMenu);
  m_List->setSelectionMode(QAbstractItemView::SingleSelection);
  m_List->setDragDropMode(QAbstractItemView::InternalMove);
  m_List->setDefaultDropAction(Qt::MoveAction);
  m_List->setMouseTracking(true);
  m_List->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_List->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_List->setUniformItemSizes(true);
  m_List->setSpacing(0);
  listLayout->addWidget(m_List, 1);

  auto* divider = new QFrame(listCard);
  divider->setObjectName(QStringLiteral("divider"));
  listLayout->addWidget(divider);

  // commands for the selected program
  auto* commands = new QHBoxLayout;
  commands->setContentsMargins(4, 0, 4, 0);
  commands->setSpacing(2);
  m_EditButton     = commandButton(tr("Edit..."), listCard);
  m_AllGamesButton = commandButton(tr("Use for all games"), listCard);
  m_AllGamesButton->setToolTip(
      tr("Gives this program every game in the list and moves it to the top, so it "
         "receives all Nexus links."));
  m_UpButton     = commandButton(tr("Move up"), listCard, false);
  m_DownButton   = commandButton(tr("Move down"), listCard, false);
  m_RemoveButton = commandButton(tr("Remove"), listCard);
  commands->addWidget(m_EditButton);
  commands->addWidget(m_AllGamesButton);
  commands->addStretch(1);
  commands->addWidget(m_UpButton);
  commands->addWidget(m_DownButton);
  commands->addWidget(m_RemoveButton);
  listLayout->addLayout(commands);
  layout->addWidget(listCard, 1);

  pageLayout->addWidget(content, 1);

  // footer band
  auto* footer = new QFrame(page);
  footer->setObjectName(QStringLiteral("footer"));
  auto* footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(24, 16, 24, 16);
  footerLayout->setSpacing(8);
  auto* saveHint = new QLabel(tr("Changes are saved when you close this window."), footer);
  saveHint->setProperty("caption", true);
  footerLayout->addWidget(saveHint, 1);
  auto* closeButton = new QPushButton(tr("Close"), footer);
  closeButton->setProperty("primary", true);
  closeButton->setDefault(true);
  closeButton->setMinimumWidth(110);
  footerLayout->addWidget(closeButton);
  pageLayout->addWidget(footer);

  connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
  connect(m_RegisterButton, &QPushButton::clicked, this, &HandlerWindow::registerActive);
  connect(removeMissingButton, &QPushButton::clicked, this, &HandlerWindow::removeMissing);
  connect(m_AddButton, &QToolButton::clicked, this, &HandlerWindow::addBinaryDialog);
  connect(m_TestButton, &QToolButton::clicked, this, &HandlerWindow::testLink);
  connect(m_EditButton, &QToolButton::clicked, this, &HandlerWindow::editBinary);
  connect(m_AllGamesButton, &QToolButton::clicked, this, &HandlerWindow::useForAllGames);
  connect(m_UpButton, &QToolButton::clicked, this, [this] { moveSelected(-1); });
  connect(m_DownButton, &QToolButton::clicked, this, [this] { moveSelected(1); });
  connect(m_RemoveButton, &QToolButton::clicked, this, &HandlerWindow::removeBinary);
  connect(m_List, &QListWidget::customContextMenuRequested, this, &HandlerWindow::showContextMenu);
  connect(m_List, &QListWidget::itemDoubleClicked, this, &HandlerWindow::editBinary);
  connect(m_List, &QListWidget::itemSelectionChanged, this, &HandlerWindow::updateState);
  connect(m_List->model(), &QAbstractItemModel::rowsMoved, this, [this] {
    m_List->viewport()->update();
    updateState();
  });

  new QShortcut(QKeySequence(Qt::Key_Delete), m_List, [this] { removeBinary(); });
  new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_N), this, [this] { addBinaryDialog(); });

  m_Built = true;
  updateIcons();
  updateRegistration();
}

HandlerWindow::~HandlerWindow() = default;

void HandlerWindow::setPrimaryHandler(const QString& handlerPath)
{
  m_PrimaryHandler = handlerPath;
  updateRegistration();
  m_List->viewport()->update();
}

void HandlerWindow::setHandlerStorage(HandlerStorage* storage)
{
  m_Storage = storage;

  m_List->clear();
  auto list = storage->handlers();
  for (auto iter = list.begin(); iter != list.end(); ++iter) {
    addItem(iter->games, QDir::toNativeSeparators(iter->executable), iter->arguments, -1);
  }
  updateState();
}

QListWidgetItem* HandlerWindow::addItem(const QStringList& games, const QString& executable,
                                        const QString& arguments, int row)
{
  auto* item = new QListWidgetItem;
  item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
  item->setData(GamesRole, games);
  item->setData(ExecutableRole, executable);
  item->setData(ArgumentsRole, arguments);

  QStringList names;
  for (const QString& game : games) {
    names.append(gameDisplayName(game));
  }
  QString tip = QStringLiteral("<b>%1</b><br>%2").arg(executable.toHtmlEscaped(),
                                                      names.join(QStringLiteral(", ")).toHtmlEscaped());
  if (!arguments.trimmed().isEmpty()) {
    tip += QStringLiteral("<br>") + tr("Arguments: %1").arg(arguments).toHtmlEscaped();
  }
  item->setToolTip(tip);

  if (row < 0 || row >= m_List->count()) {
    m_List->addItem(item);
  } else {
    m_List->insertItem(row, item);
  }
  return item;
}

std::list<HandlerInfo> HandlerWindow::currentHandlers() const
{
  std::list<HandlerInfo> result;
  for (int i = 0; i < m_List->count(); ++i) {
    const QListWidgetItem* item = m_List->item(i);
    HandlerInfo info;
    info.ID         = i;
    info.games      = item->data(GamesRole).toStringList();
    info.executable = item->data(ExecutableRole).toString();
    info.arguments  = item->data(ArgumentsRole).toString();
    result.push_back(info);
  }
  return result;
}

void HandlerWindow::closeEvent(QCloseEvent* event)
{
  // same as upstream: the list replaces the stored handlers, top to bottom
  m_Storage->clear();
  for (int i = 0; i < m_List->count(); ++i) {
    const QListWidgetItem* item = m_List->item(i);
    m_Storage->registerHandler(item->data(GamesRole).toStringList(),
                               item->data(ExecutableRole).toString(),
                               item->data(ArgumentsRole).toString(), false, false);
  }
  QMainWindow::closeEvent(event);
}

void HandlerWindow::changeEvent(QEvent* event)
{
  // Windows switched between light and dark: redraw the glyphs in the new colours
  if (m_Built &&
      (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)) {
    updateIcons();
    updateRegistration();
  }
  QMainWindow::changeEvent(event);
}

void HandlerWindow::addBinaryDialog()
{
  HandlerEditDialog dialog(HandlerEditDialog::Mode::Add, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  // as upstream: a program already in the list gets the new games (and the
  // new arguments), anything else goes to the top
  bool executableKnown = false;
  for (int i = 0; i < m_List->count(); ++i) {
    QListWidgetItem* item = m_List->item(i);
    if (QFileInfo(item->data(ExecutableRole).toString()) == QFileInfo(dialog.executable())) {
      QStringList games = item->data(GamesRole).toStringList();
      games.append(dialog.gameIDs());
      games.removeDuplicates();
      const QString arguments = item->data(ArgumentsRole).toString();
      const QString newArguments =
          arguments.compare(dialog.arguments(), Qt::CaseInsensitive) != 0 ? dialog.arguments()
                                                                          : arguments;
      const QString executable = item->data(ExecutableRole).toString();
      delete m_List->takeItem(i);
      m_List->setCurrentItem(addItem(games, executable, newArguments, i));
      executableKnown = true;
      break;
    }
  }

  if (!executableKnown) {
    m_List->setCurrentItem(addItem(dialog.gameIDs(), dialog.executable(), dialog.arguments(), 0));
  }
  updateState();
}

void HandlerWindow::editBinary()
{
  QListWidgetItem* item = m_List->currentItem();
  if (item == nullptr) {
    return;
  }
  HandlerEditDialog dialog(HandlerEditDialog::Mode::Edit, this);
  dialog.setValues(item->data(GamesRole).toStringList(), item->data(ExecutableRole).toString(),
                   item->data(ArgumentsRole).toString());
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const int row = m_List->row(item);
  delete m_List->takeItem(row);
  m_List->setCurrentItem(addItem(dialog.gameIDs(), dialog.executable(), dialog.arguments(), row));
  updateState();
}

void HandlerWindow::removeBinary()
{
  const int row = m_List->currentRow();
  if (row < 0) {
    return;
  }
  delete m_List->takeItem(row);
  updateState();
}

void HandlerWindow::removeMissing()
{
  for (int i = m_List->count() - 1; i >= 0; --i) {
    if (entryStatus(m_List->item(i)->data(ExecutableRole).toString(), m_PrimaryHandler) ==
        EntryStatus::Missing) {
      delete m_List->takeItem(i);
    }
  }
  updateState();
}

void HandlerWindow::moveSelected(int delta)
{
  const int row    = m_List->currentRow();
  const int target = row + delta;
  if (row < 0 || target < 0 || target >= m_List->count()) {
    return;
  }
  QListWidgetItem* item = m_List->takeItem(row);
  m_List->insertItem(target, item);
  m_List->setCurrentItem(item);
  m_List->viewport()->update();
  updateState();
}

void HandlerWindow::useForAllGames()
{
  QListWidgetItem* item = m_List->currentItem();
  if (item == nullptr) {
    return;
  }

  // every known game (the registration default), "other", and every game any
  // program in the list handles; at the top it then wins every lookup
  QStringList games = item->data(GamesRole).toStringList();
  for (const auto& game : HandlerStorage::knownGames()) {
    games.append(std::get<1>(game));
  }
  for (int i = 0; i < m_List->count(); ++i) {
    games.append(m_List->item(i)->data(GamesRole).toStringList());
  }
  QStringList unique;
  for (const QString& game : games) {
    const QString id = game.trimmed().toLower();
    if (!id.isEmpty() && !unique.contains(id)) {
      unique.append(id);
    }
  }

  const QString executable = item->data(ExecutableRole).toString();
  const QString arguments  = item->data(ArgumentsRole).toString();
  delete m_List->takeItem(m_List->row(item));
  m_List->setCurrentItem(addItem(unique, executable, arguments, 0));
  m_List->scrollToTop();
  updateState();
}

void HandlerWindow::testLink()
{
  LinkTestDialog dialog(currentHandlers(), registeredElsewhere(), this);
  dialog.exec();
}

void HandlerWindow::registerActive()
{
  if (QMessageBox::question(
          this, tr("Change handler registration?"),
          tr("This will make the nxmhandler.exe you called the primary handler registered in "
             "the system.\nThat has no immediate impact on how links are handled.\nUse this if "
             "you moved Mod Organizer or if you uninstalled the Mod Organizer installation "
             "that was previously registered. Continue?"),
          QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    m_Storage->registerProxy(QCoreApplication::applicationFilePath());
    setPrimaryHandler(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
  }
}

void HandlerWindow::showContextMenu(const QPoint& pos)
{
  QMenu menu(this);
  const Fluent::Theme& t = Fluent::theme();
  QListWidgetItem* item  = m_List->itemAt(pos);
  if (item != nullptr) {
    m_List->setCurrentItem(item);
    menu.addAction(Fluent::glyphIcon(GlyphEdit, t.text), tr("Edit..."), this,
                   &HandlerWindow::editBinary);
    menu.addAction(Fluent::glyphIcon(GlyphAll, t.text), tr("Use for all games"), this,
                   &HandlerWindow::useForAllGames);
    QAction* up = menu.addAction(Fluent::glyphIcon(GlyphUp, t.text), tr("Move up"), this,
                                 [this] { moveSelected(-1); });
    up->setEnabled(m_List->row(item) > 0);
    QAction* down = menu.addAction(Fluent::glyphIcon(GlyphDown, t.text), tr("Move down"), this,
                                   [this] { moveSelected(1); });
    down->setEnabled(m_List->row(item) < m_List->count() - 1);
    menu.addSeparator();
    menu.addAction(Fluent::glyphIcon(GlyphDelete, t.text), tr("Remove"), this,
                   &HandlerWindow::removeBinary);
  } else {
    menu.addAction(Fluent::glyphIcon(GlyphAdd, t.text), tr("Add program..."), this,
                   &HandlerWindow::addBinaryDialog);
    menu.addAction(Fluent::glyphIcon(GlyphTest, t.text), tr("Test a link..."), this,
                   &HandlerWindow::testLink);
  }
  if (missingCount() > 0) {
    menu.addSeparator();
    menu.addAction(tr("Remove missing"), this, &HandlerWindow::removeMissing);
  }
  menu.exec(m_List->viewport()->mapToGlobal(pos));
}

int HandlerWindow::missingCount() const
{
  int count = 0;
  for (int i = 0; i < m_List->count(); ++i) {
    if (entryStatus(m_List->item(i)->data(ExecutableRole).toString(), m_PrimaryHandler) ==
        EntryStatus::Missing) {
      ++count;
    }
  }
  return count;
}

// the registered program when it is not an nxmhandler (Vortex, a browser
// helper...), described for people; empty otherwise
QString HandlerWindow::registeredElsewhere() const
{
  if (m_PrimaryHandler.isEmpty()) {
    return tr("no program");
  }
  if (isNxmHandler(m_PrimaryHandler)) {
    return {};
  }
  return InstallInfo::describe(m_PrimaryHandler).title();
}

void HandlerWindow::updateRegistration()
{
  const Fluent::Theme& t = Fluent::theme();
  const QString self     = QCoreApplication::applicationFilePath();
  const QString path     = m_PrimaryHandler;

  QString value, where, status, kind;
  bool offerRegister = true;
  const auto describeInstall = [](const QString& nxmhandler) {
    const InstallInfo mo = InstallInfo::describe(
        QFileInfo(nxmhandler).absoluteDir().filePath(QStringLiteral("ModOrganizer.exe")));
    return mo.exists ? entryTitle(mo) : QStringLiteral("Mod Organizer");
  };
  if (path.isEmpty()) {
    value  = tr("No program");
    status = tr("Not registered");
    kind   = QStringLiteral("crit");
  } else if (samePath(path, self)) {
    // the installation this nxmhandler belongs to
    value         = describeInstall(self);
    where         = tr("This installation");
    status        = tr("Registered");
    kind          = QStringLiteral("ok");
    offerRegister = false;
  } else if (!QFileInfo(path).isFile()) {
    value  = tr("A program that no longer exists");
    status = tr("Missing");
    kind   = QStringLiteral("crit");
  } else if (isNxmHandler(path)) {
    value  = describeInstall(path);
    where  = tr("Another Mod Organizer installation");
    status = tr("Other installation");
    kind   = QStringLiteral("warn");
  } else {
    value  = InstallInfo::describe(path).title();
    where  = tr("Another program");
    status = tr("Another program");
    kind   = QStringLiteral("warn");
  }

  QString detail = QDir::toNativeSeparators(path);
  if (!where.isEmpty()) {
    detail = QStringLiteral("%1  ·  %2").arg(where, detail);
  }
  m_RegistrationValue->setText(value);
  m_RegistrationPath->setText(detail);
  m_RegistrationPath->setVisible(!path.isEmpty());
  m_RegistrationStatus->setText(status);
  m_RegistrationStatus->setProperty("kind", kind);
  m_RegistrationStatus->style()->unpolish(m_RegistrationStatus);
  m_RegistrationStatus->style()->polish(m_RegistrationStatus);

  // a foreign program (Vortex...) or nothing: taking the links back is the
  // main thing to do here; another installation: possible, rarely needed
  m_RegisterButton->setVisible(offerRegister);
  m_RegisterButton->setText(path.isEmpty() || !isNxmHandler(path) ? tr("Take links back")
                                                                  : tr("Register this installation"));
  m_RegisterButton->setProperty("primary", kind == QLatin1String("crit") ||
                                               (!path.isEmpty() && !isNxmHandler(path)));
  m_RegisterButton->style()->unpolish(m_RegisterButton);
  m_RegisterButton->style()->polish(m_RegisterButton);

  const QColor iconColor = kind == QLatin1String("ok")     ? t.accentText
                           : kind == QLatin1String("warn") ? t.warnFg
                                                           : t.critFg;
  m_RegistrationIcon->setPixmap(Fluent::glyphIcon(GlyphLink, iconColor, 20).pixmap(20, 20));

  updateState();
}

void HandlerWindow::updateState()
{
  const int row   = m_List->currentRow();
  const bool any  = row >= 0 && m_List->currentItem() != nullptr &&
                    m_List->currentItem()->isSelected();
  m_EditButton->setEnabled(any);
  m_AllGamesButton->setEnabled(any);
  m_RemoveButton->setEnabled(any);
  m_UpButton->setEnabled(any && row > 0);
  m_DownButton->setEnabled(any && row < m_List->count() - 1);

  const int missing = missingCount();
  m_MissingBar->setVisible(missing > 0);
  // English gets no translation file, so the singular is its own string and
  // %n stays a real plural for every other language
  m_MissingText->setText(
      missing == 1
          ? tr("A program in this list no longer exists. Links for its games cannot be "
               "opened.")
          : tr("%n programs in this list no longer exist. Links for their games cannot "
               "be opened.",
               nullptr, missing));
}

void HandlerWindow::updateIcons()
{
  const Fluent::Theme& t = Fluent::theme();
  m_AddButton->setIcon(Fluent::glyphIcon(GlyphAdd, t.text));
  m_TestButton->setIcon(Fluent::glyphIcon(GlyphTest, t.text));
  m_EditButton->setIcon(Fluent::glyphIcon(GlyphEdit, t.text));
  m_AllGamesButton->setIcon(Fluent::glyphIcon(GlyphAll, t.text));
  m_UpButton->setIcon(Fluent::glyphIcon(GlyphUp, t.text));
  m_DownButton->setIcon(Fluent::glyphIcon(GlyphDown, t.text));
  m_RemoveButton->setIcon(Fluent::glyphIcon(GlyphDelete, t.text));
  m_MissingIcon->setPixmap(Fluent::glyphIcon(GlyphWarning, t.warnFg, 20).pixmap(20, 20));
}
