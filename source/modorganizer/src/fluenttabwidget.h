#ifndef FLUENTTABWIDGET_H
#define FLUENTTABWIDGET_H

#include <QColor>
#include <QRectF>
#include <QTabBar>
#include <QTabWidget>

class QVariantAnimation;

// tab bar with the Windows 11 selection indicator: a short accent pill under
// the selected tab that slides to the next tab when the selection changes
//
// the color comes from the stylesheet:
//   FluentTabBar { qproperty-indicatorColor: #005FB8; }
//
class FluentTabBar : public QTabBar
{
  Q_OBJECT
  Q_PROPERTY(QColor indicatorColor READ indicatorColor WRITE setIndicatorColor)

public:
  explicit FluentTabBar(QWidget* parent = nullptr);

  QColor indicatorColor() const;
  void setIndicatorColor(const QColor& c);

protected:
  void paintEvent(QPaintEvent* e) override;
  void resizeEvent(QResizeEvent* e) override;
  void tabLayoutChange() override;
  void showEvent(QShowEvent* e) override;

private:
  QColor m_color;
  QRectF m_indicator;
  QVariantAnimation* m_animation;

  QRectF indicatorRect(int index) const;
  void moveIndicator(bool animate);
};

// QTabWidget::setTabBar() is protected, so the main window promotes its tab
// widget to this class in the .ui file
class FluentTabWidget : public QTabWidget
{
  Q_OBJECT

public:
  explicit FluentTabWidget(QWidget* parent = nullptr);
};

#endif  // FLUENTTABWIDGET_H
