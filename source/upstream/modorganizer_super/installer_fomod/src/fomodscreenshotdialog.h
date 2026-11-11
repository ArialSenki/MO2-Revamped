#pragma once

#include <QDialog>
#include <QString>

#include <utility>
#include <vector>

class QLabel;
class QListWidget;
class QPushButton;
class ScaleLabel;

/**
 * @brief The pictures of a FOMOD step's options, large: a dark overlay over
 * the screen with the picture, its option's name, the previous and next
 * pictures and a strip of all of them. The arrow keys move through them and
 * Escape closes it. MO2 Revamped builds it in code instead of 2.5.2's .ui.
 **/
class FomodScreenshotDialog : public QDialog
{
  Q_OBJECT

public:
  /**
   * @param carouselImages each option's name and the path to its picture
   * @param carouselIndex the picture shown first
   **/
  explicit FomodScreenshotDialog(
      QWidget* parent, std::vector<std::pair<QString, QString>> carouselImages,
      int carouselIndex);

protected:
  void paintEvent(QPaintEvent* event) override;
  // the arrow keys move through the pictures from the buttons too, which
  // would move the focus with them
  bool eventFilter(QObject* watched, QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

private:
  void showPicture(int index);

  std::vector<std::pair<QString, QString>> m_carouselImages;
  int m_index = -1;
  ScaleLabel* m_image;
  QLabel* m_title;
  QLabel* m_position;
  QPushButton* m_previous;
  QPushButton* m_next;
  QListWidget* m_strip;
};
