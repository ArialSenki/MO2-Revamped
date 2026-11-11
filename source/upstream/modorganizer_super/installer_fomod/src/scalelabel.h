#ifndef SCALELABEL_H
#define SCALELABEL_H

#include <QCache>
#include <QImage>
#include <QLabel>
#include <QMovie>

class ScaleLabel : public QLabel
{
  Q_OBJECT
public:
  explicit ScaleLabel(QWidget* parent = nullptr);

  void setScalableResource(const QString& path);
  void setStatic(bool isStatic);
signals:

public slots:
protected:
  virtual void resizeEvent(QResizeEvent* event);

private:
  void setScalableMovie(const QString& path);
  void setScalableImage(const QString& path);

  QImage m_UnscaledImage;
  // the pictures this label read, cost in kilobytes
  QCache<QString, QImage> m_Cache{96 * 1024};
  QSize m_OriginalMovieSize;
  bool m_isStatic = false;
};

#endif  // SCALELABEL_H
