#include "themeeditordialog.h"

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <log.h>

using namespace MOBase;

namespace
{

QString colorLabel(const QString& key)
{
  if (key == QStringLiteral("accent")) {
    return QObject::tr("Accent");
  } else if (key == QStringLiteral("window")) {
    return QObject::tr("Window background");
  } else if (key == QStringLiteral("surface")) {
    return QObject::tr("Cards and lists");
  } else if (key == QStringLiteral("border")) {
    return QObject::tr("Borders");
  } else if (key == QStringLiteral("text")) {
    return QObject::tr("Text");
  } else if (key == QStringLiteral("muted")) {
    return QObject::tr("Secondary text");
  }
  return key;
}

QIcon swatchIcon(const QColor& color)
{
  const qreal ratio = 2.0;
  QPixmap pm(QSize(28, 18) * ratio);
  pm.setDevicePixelRatio(ratio);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(QColor(128, 128, 128, 160), 1.0));
  p.setBrush(color);
  p.drawRoundedRect(QRectF(0.5, 0.5, 27, 17), 4, 4);
  return QIcon(pm);
}

// counts the editor's openings: a closed editor's delayed clean-up knows
// whether another one has been opened since
int& editorSession()
{
  static int session = 0;
  return session;
}

}  // namespace

ThemeEditorDialog::ThemeEditorDialog(QWidget* parent, const QString& stylesheetsDir,
                                     const QString& styleInUse,
                                     const UserTheme::Theme* existing, ApplyStyle apply)
    : FluentContentDialog(parent, QStringLiteral("ThemeEditorDialog"),
                          existing ? tr("Edit theme") : tr("New theme"),
                          tr("Pick the colors: MO2 shows them as you change them, and "
                             "Cancel puts back the theme you had.")),
      m_dir(stylesheetsDir), m_styleInUse(styleInUse), m_apply(std::move(apply))
{
  ++editorSession();
  if (existing) {
    m_existingFile = existing->fileName;
    m_rules        = UserTheme::rulesOf(existing->path);
    m_dark         = existing->dark;
  } else {
    // a new theme starts from the theme in use
    m_dark = styleInUse.compare(QStringLiteral("Light.qss"), Qt::CaseInsensitive) != 0;
  }
  m_colors = UserTheme::builtInColors(m_dark);
  if (existing) {
    for (auto it = existing->colors.begin(); it != existing->colors.end(); ++it) {
      m_colors.insert(it.key(), it.value());
    }
  }

  auto* grid = new QGridLayout;
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(8);
  grid->setColumnStretch(2, 1);
  int row = 0;

  grid->addWidget(new QLabel(tr("Name"), this), row, 0);
  m_name = new QLineEdit(existing ? existing->name : tr("My theme"), this);
  // screen readers name the fields by their labels, as the colors' are
  m_name->setAccessibleName(tr("Name"));
  m_name->setMaxLength(60);
  grid->addWidget(m_name, row++, 1, 1, 2);

  grid->addWidget(new QLabel(tr("Based on"), this), row, 0);
  m_base = new QComboBox(this);
  m_base->setAccessibleName(tr("Based on"));
  m_base->addItem(tr("Dark"), true);
  m_base->addItem(tr("Light"), false);
  m_base->setCurrentIndex(m_dark ? 0 : 1);
  grid->addWidget(m_base, row++, 1, 1, 2);

  const QRegularExpression hexPattern(QStringLiteral("#?[0-9A-Fa-f]{0,6}"));
  for (const QString& key : UserTheme::colorKeys()) {
    ColorRow r;
    grid->addWidget(new QLabel(colorLabel(key), this), row, 0);

    r.swatch = new QPushButton(this);
    r.swatch->setIconSize(QSize(28, 18));
    r.swatch->setToolTip(tr("Pick a color"));
    r.swatch->setAccessibleName(tr("Pick a color"));
    // the swatch is the color itself: not tinted like the other buttons' glyphs
    r.swatch->setProperty("fluentKeepIcon", true);
    grid->addWidget(r.swatch, row, 1);

    r.hex = new QLineEdit(this);
    r.hex->setValidator(new QRegularExpressionValidator(hexPattern, r.hex));
    r.hex->setMaximumWidth(110);
    r.hex->setAccessibleName(colorLabel(key));
    grid->addWidget(r.hex, row, 2, Qt::AlignLeft);
    ++row;

    m_rows.insert(key, r);
    connect(r.swatch, &QPushButton::clicked, this, [this, key] {
      pickColor(key);
    });
    // a full color applies as it's typed
    connect(r.hex, &QLineEdit::textChanged, this, [this, key](const QString& text) {
      const QString digits = text.trimmed().startsWith('#') ? text.trimmed().mid(1)
                                                             : text.trimmed();
      if (digits.size() == 6) {
        setColor(key, QColor(QStringLiteral("#") + digits));
      }
    });
    connect(r.hex, &QLineEdit::editingFinished, this, [this, key] {
      const QString text = m_rows.value(key).hex->text().trimmed();
      const QColor c(text.startsWith('#') ? text : QStringLiteral("#") + text);
      if (c.isValid() && text.size() >= 6) {
        setColor(key, c);
      } else {
        setColor(key, m_colors.value(key));  // back to the last good one
      }
    });
  }
  body()->addLayout(grid);

  if (!m_rules.trimmed().isEmpty()) {
    auto* note = new QLabel(
        tr("This theme also has rules of its own. They are kept as they are."), this);
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("fluentDialogText"));
    body()->addWidget(note);
  }

  for (const QString& key : UserTheme::colorKeys()) {
    setColor(key, m_colors.value(key));
  }

  auto* saveButton   = addButton(tr("Save"), true);
  auto* cancelButton = addButton(tr("Cancel"));
  connect(saveButton, &QPushButton::clicked, this, [this] {
    if (save()) {
      accept();
    }
  });
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_base, &QComboBox::currentIndexChanged, this, [this] {
    baseChanged();
  });

  // applying a theme parses the whole style sheet: changes wait for a pause
  m_previewTimer.setSingleShot(true);
  m_previewTimer.setInterval(250);
  connect(&m_previewTimer, &QTimer::timeout, this, [this] {
    preview();
  });

  // an edited theme shows at once; a new one when a color changes
  if (existing) {
    schedulePreview();
  }
}

ThemeEditorDialog::~ThemeEditorDialog() = default;

UserTheme::Theme ThemeEditorDialog::theme() const
{
  UserTheme::Theme t;
  t.name = m_name->text().trimmed().isEmpty() ? tr("My theme") : m_name->text().trimmed();
  t.dark = m_dark;
  const auto base = UserTheme::builtInColors(m_dark);
  for (const QString& key : UserTheme::colorKeys()) {
    // only what differs from the base: the file stays short, and a key left
    // out keeps following Light or Dark
    if (m_colors.value(key) != base.value(key)) {
      t.colors.insert(key, m_colors.value(key));
    }
  }
  return t;
}

void ThemeEditorDialog::setColor(const QString& key, const QColor& color)
{
  const QColor c(color.rgb());
  const bool changed = m_colors.value(key) != c;
  m_colors.insert(key, c);

  const ColorRow& r = m_rows.value(key);
  r.swatch->setIcon(swatchIcon(c));
  // not while it's being typed in: the caret would jump
  const QString name = c.name(QColor::HexRgb).toUpper();
  if (r.hex->text().compare(name, Qt::CaseInsensitive) != 0 &&
      r.hex->text().compare(name.mid(1), Qt::CaseInsensitive) != 0) {
    r.hex->setText(name);
  }

  if (changed) {
    schedulePreview();
  }
}

void ThemeEditorDialog::pickColor(const QString& key)
{
  QColorDialog dialog(m_colors.value(key), this);
  dialog.setWindowTitle(colorLabel(key));
  // the color shows on MO2 while it's being picked
  connect(&dialog, &QColorDialog::currentColorChanged, this, [this, key](const QColor& c) {
    if (c.isValid()) {
      setColor(key, c);
    }
  });
  const QColor before = m_colors.value(key);
  if (dialog.exec() == QDialog::Accepted) {
    setColor(key, dialog.selectedColor());
  } else {
    setColor(key, before);
  }
}

void ThemeEditorDialog::baseChanged()
{
  const bool dark = m_base->currentData().toBool();
  if (dark == m_dark) {
    return;
  }
  // colors still at the old base's follow the new one; picked ones stay
  const auto oldBase = UserTheme::builtInColors(m_dark);
  const auto newBase = UserTheme::builtInColors(dark);
  m_dark             = dark;
  for (const QString& key : UserTheme::colorKeys()) {
    if (m_colors.value(key) == oldBase.value(key)) {
      setColor(key, newBase.value(key));
    }
  }
  schedulePreview();
}

void ThemeEditorDialog::schedulePreview()
{
  m_previewTimer.start();
}

void ThemeEditorDialog::preview()
{
  const QString fileName = UserTheme::previewFileName(++m_previewTurn);
  QString error;
  if (!UserTheme::write(QDir(m_dir).filePath(fileName), theme(), m_rules, &error)) {
    log::warn("can't write the theme preview: {}", error);
    return;
  }
  m_previewed = true;
  m_apply(fileName);
}

void ThemeEditorDialog::removePreviews()
{
  // a little later: a preview MO2 hasn't applied yet still reads its file.
  // Not if the editor was opened again meanwhile: its previews have the same
  // names, and it removes them when it closes
  QTimer::singleShot(3000, qApp, [dir = m_dir, session = editorSession()] {
    if (session != editorSession()) {
      return;
    }
    for (int turn = 0; turn < 2; ++turn) {
      QFile::remove(QDir(dir).filePath(UserTheme::previewFileName(turn)));
    }
  });
}

bool ThemeEditorDialog::save()
{
  m_previewTimer.stop();

  const UserTheme::Theme t = theme();
  const QString fileName =
      m_existingFile.isEmpty() ? UserTheme::newFileName(m_dir, t.name) : m_existingFile;

  QString error;
  if (!UserTheme::write(QDir(m_dir).filePath(fileName), t, m_rules, &error)) {
    QMessageBox::warning(
        this, tr("Theme not saved"),
        tr("MO2 couldn't write the theme to %1:\n%2\n\nIf MO2 Revamped is installed "
           "in a protected folder such as Program Files, run it as administrator "
           "once to save themes there, or install it in another folder.")
            .arg(QDir::toNativeSeparators(QDir(m_dir).filePath(fileName)), error));
    return false;
  }

  log::info("saved theme '{}' to {}", t.name, fileName);
  m_saved = fileName;
  return true;
}

void ThemeEditorDialog::done(int result)
{
  m_previewTimer.stop();
  if (!m_saved.isEmpty()) {
    m_apply(m_saved);
  } else if (m_previewed) {
    // closed without saving: the theme that was in use comes back
    m_apply(m_styleInUse);
  }
  removePreviews();
  FluentContentDialog::done(result);
}
