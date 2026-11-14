#ifndef COLLECTIONBROWSER_H
#define COLLECTIONBROWSER_H

// MO2 Revamped: the managed game's Nexus collections, from GraphQL
// collectionsV2: search, sort, 20 at a time. Open sends one to the
// collection window.

#include <QDialog>
#include <QVariant>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

class CollectionBrowser : public QDialog
{
  Q_OBJECT

public:
  // open gets the collection's address
  CollectionBrowser(const QString& gameDomain, const QString& gameName,
                    std::function<void(const QString&)> open, QWidget* parent);

private:
  QString m_domain;
  std::function<void(const QString&)> m_open;
  QLineEdit* m_search;
  QComboBox* m_sort;
  QTreeWidget* m_list;
  QLabel* m_status;
  QPushButton* m_more;
  QPushButton* m_openButton;
  int m_request = -1;
  int m_loaded  = 0;
  int m_total   = 0;

  void search(bool more);
  void onResult(QVariant userData, QVariant result, int id);
  void onFailed(QString, int, int, QVariant, int id, int code, const QString& error);
  void openSelected();
};

#endif  // COLLECTIONBROWSER_H
