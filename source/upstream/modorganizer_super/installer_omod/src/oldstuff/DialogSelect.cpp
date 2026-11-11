#include "DialogSelect.h"

#include <functional>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDebug>
#include <QImageReader>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "MIT-licencedCodeToDoStuff/checkboxwordwrap.h"
#include "fluentcontentdialog.h"

#include <QLabel>
#include <log.h>

// there is no hover signal, the only way to know is to override enterEvent()
//
template <class T, typename... Args>
class HoverableWidget : public T
{
public:
  std::function<void ()> onHover;

  HoverableWidget(Args... args, std::function<void ()> h)
    : T(args...), onHover(std::move(h))
  {
  }

protected:
  void enterEvent(QEnterEvent*) override
  {
    onHover();
  }

  void focusInEvent(QFocusEvent* event) override
  {
    T::focusInEvent(event);
    onHover();
  }
};


FixedAspectRatioImageLabel::FixedAspectRatioImageLabel(QWidget* parent) : QLabel(parent)
{
}

void FixedAspectRatioImageLabel::setUnscaledPixmap(const QPixmap& pixmap)
{
  mUnscaledPixmap = pixmap;
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  rescalePixmap(size());

  copySizeLabel.setPixmap(pixmap);
}

const QPixmap& FixedAspectRatioImageLabel::unscaledPixmap() const
{
    return mUnscaledPixmap;
}

QSize FixedAspectRatioImageLabel::sizeHint() const
{
  if (mUnscaledPixmap.isNull())
  {
    return QLabel::sizeHint();
  }
  else
  {
    // maybe should add frame border
    return mUnscaledPixmap.size();
    //return copySizeLabel.sizeHint();
  }
}

bool FixedAspectRatioImageLabel::hasHeightForWidth() const
{
  return true;
}

int FixedAspectRatioImageLabel::heightForWidth(int width) const
{
  // this ignores the difference between size and contentsRect size
  return mUnscaledPixmap.height() * width / (double)mUnscaledPixmap.width();
}

int FixedAspectRatioImageLabel::widthForHeight(int height) const
{
  return mUnscaledPixmap.width() * height / (double)mUnscaledPixmap.height();
}

void FixedAspectRatioImageLabel::resizeEvent(QResizeEvent* resizeEvent)
{
  QLabel::resizeEvent(resizeEvent);
  rescalePixmap(contentsRect().size());
}

void FixedAspectRatioImageLabel::rescalePixmap(const QSize& size)
{
  setPixmap(mUnscaledPixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}


std::optional<QVector<int>> DialogSelect(
  QWidget* parent, const QString& title, const QVector<QString>& items,
  const QVector<QString>& descriptions, const QVector<QString>& pixmaps,
  bool multiSelect)
{
  // MO2 Revamped: the OMOD script's question as a Windows 11 content dialog:
  // its options on the left (check boxes when several can be chosen), the
  // hovered or focused option's picture and description on the right
  RevampedFluent::ContentDialog d(parent, QStringLiteral("omodSelectDialog"), title,
                                  multiSelect ? QCoreApplication::translate("OmodDialogSelect", "Choose any.")
                                              : QCoreApplication::translate("OmodDialogSelect", "Choose one."));
  d.setMinimumSize(640, 420);
  d.resize(860, 560);
  d.setSizeGripEnabled(true);
  d.setWindowFlag(Qt::WindowMaximizeButtonHint, true);

  auto* splitter = new QSplitter(&d);
  splitter->setChildrenCollapsible(false);
  d.body()->addWidget(splitter, 1);

  // the options scroll when there are many
  auto* optionsScroll = new QScrollArea(splitter);
  optionsScroll->setObjectName(QStringLiteral("omodOptionsScroll"));
  optionsScroll->setFrameShape(QFrame::NoFrame);
  optionsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  optionsScroll->setWidgetResizable(true);
  auto* options = new QWidget(optionsScroll);
  auto* optionsLayout = new QVBoxLayout(options);
  optionsLayout->setContentsMargins(0, 0, 12, 0);
  optionsLayout->setSpacing(6);
  optionsScroll->setWidget(options);

  // the details: only what the script gave
  const bool hasDescriptions = !descriptions.empty();
  const bool hasPixmaps = !pixmaps.empty();
  auto* details = new QFrame(splitter);
  details->setProperty("fluentCard", true);
  auto* detailsLayout = new QVBoxLayout(details);
  detailsLayout->setContentsMargins(16, 14, 16, 14);
  detailsLayout->setSpacing(10);
  auto* picture = new FixedAspectRatioImageLabel(details);
  picture->setAlignment(Qt::AlignCenter);
  picture->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  picture->setMinimumHeight(140);
  picture->setVisible(hasPixmaps);
  detailsLayout->addWidget(picture, 3);
  auto* optionName = new QLabel(details);
  optionName->setObjectName(QStringLiteral("instanceDetailName"));
  optionName->setTextFormat(Qt::PlainText);
  optionName->setWordWrap(true);
  detailsLayout->addWidget(optionName);
  auto* description = new QPlainTextEdit(details);
  description->setObjectName(QStringLiteral("omodDescription"));
  description->setReadOnly(true);
  description->setFrameShape(QFrame::NoFrame);
  description->setAccessibleName(QCoreApplication::translate("OmodDialogSelect", "Option description"));
  description->setVisible(hasDescriptions);
  detailsLayout->addWidget(description, 2);
  if (!hasPixmaps) {
    detailsLayout->addStretch(1);
  }
  splitter->addWidget(optionsScroll);
  splitter->addWidget(details);
  splitter->setStretchFactor(0, 2);
  splitter->setStretchFactor(1, 3);

  // pictures are read once, when first shown
  QVector<QPixmap> loaded(pixmaps.size());
  QVector<bool> tried(pixmaps.size(), false);
  QVector<QString> labels;

  auto show = [&](int i) {
    optionName->setText(i < labels.size() ? labels[i] : QString());
    description->setPlainText(i < descriptions.size() ? descriptions[i] : QString());
    if (hasPixmaps) {
      if (i < pixmaps.size() && !tried[i]) {
        tried[i] = true;
        // QImageReader decides the format from the content: QPixmap goes by
        // the extension, which OMODs get wrong
        QImageReader reader(pixmaps[i]);
        reader.setDecideFormatFromContent(true);
        const QImage image = reader.read();
        if (!image.isNull()) {
          loaded[i] = QPixmap::fromImage(image);
        } else {
          MOBase::log::warn("OMOD picture '{}' couldn't be read: {}", pixmaps[i],
                            reader.errorString());
        }
      }
      const QPixmap pixmap = i < loaded.size() ? loaded[i] : QPixmap();
      picture->setUnscaledPixmap(pixmap);
      picture->setVisible(!pixmap.isNull());
      picture->setAccessibleName(i < labels.size() ? labels[i] : QString());
    }
  };

  // remember buttons to see which were checked
  QVector<QAbstractButton*> itemButtons;
  for (int i = 0; i < items.size(); ++i) {
    QString labelText = items[i];
    bool checked = false;

    // THIS IS IMPORTANT. OMODS HAVE A PIPE AT THE START OF ANY OPTIONS
    // SELECTED BY DEFAULT.
    if (labelText.startsWith('|')) {
      labelText = labelText.remove(0, 1);
      checked = true;
    }
    labels.push_back(labelText);

    QAbstractButton* w = nullptr;
    // the hovered option shows, and so does the focused one: 2.5.2 only
    // followed the mouse
    if (multiSelect) {
      w = new HoverableWidget<CheckBoxWordWrap, const QString&, QWidget*>(labelText, options, [&, i] { show(i); });
    } else {
      w = new HoverableWidget<RadioButtonWordWrap, const QString&, QWidget*>(labelText, options, [&, i] { show(i); });
      checked |= i == 0;
    }
    w->setAccessibleName(labelText);
    optionsLayout->addWidget(w);
    itemButtons.push_back(w);
    w->setChecked(checked);
  }
  // push all the items to the top
  optionsLayout->addStretch(1);

  auto* ok = d.addButton(QCoreApplication::translate("OmodDialogSelect", "OK"), true);
  auto* cancel = d.addButton(QCoreApplication::translate("OmodDialogSelect", "Cancel"));
  // string connects: C++/CLI finds no signal for a function pointer
  QObject::connect(ok, SIGNAL(clicked()), &d, SLOT(accept()));
  QObject::connect(cancel, SIGNAL(clicked()), &d, SLOT(reject()));

  // the first chosen option shows to begin with
  int first = 0;
  for (int i = 0; i < itemButtons.size(); ++i) {
    if (itemButtons[i]->isChecked()) {
      first = i;
      break;
    }
  }
  if (!itemButtons.isEmpty()) {
    show(first);
    itemButtons[first]->setFocus();
  }

  const bool accepted = d.exec() == QDialog::Accepted;

  // go through every button, it doesn't matter whether they're checkboxes or
  // radio buttons
  QVector<int> selection;
  for (int i = 0; i < itemButtons.size(); ++i) {
    if (itemButtons[i]->isChecked()) {
      selection.push_back(i);
    }
  }
  // the buttons' hover and focus callbacks use this function's locals: the
  // buttons go before them, not with the dialog after them
  d.hide();
  qDeleteAll(itemButtons);

  if (!accepted) {
    return std::nullopt;
  }
  return selection;
}
