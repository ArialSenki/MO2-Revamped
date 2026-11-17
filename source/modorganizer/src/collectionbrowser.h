#ifndef COLLECTIONBROWSER_H
#define COLLECTIONBROWSER_H

// MO2 Revamped: the managed game's Nexus collections, from GraphQL
// collectionsV2: search, sort, 20 at a time. Laid out as the Wabbajack lists'
// gallery: a picture on each row, the chosen one's details under the table,
// and Open sends it to the collection window.

#include <QDialog>
#include <QHash>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QSet>
#include <QVariant>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace CollectionImages
{
// a collection's picture from Nexus' media server: https and nexusmods.com
// only, at most a few MB; done gets a null image when it can't be read
void fetch(QNetworkAccessManager& network, const QString& url, QObject* context,
           std::function<void(const QImage&)> done);
}  // namespace CollectionImages

class CollectionBrowser : public QDialog
{
  Q_OBJECT

public:
  // open gets the collection's address
  CollectionBrowser(const QString& gameDomain, const QString& gameName,
                    std::function<void(const QString&)> open, QWidget* parent);

private:
  // what a row shows in the details under the table
  struct Entry
  {
    QString slug;
    QString name;
    QString author;
    QString summary;
    QString category;
    QString image;
    QStringList gameVersions;
    int revision     = 0;
    int mods         = 0;
    qint64 size      = 0;
    int endorsements = 0;
    int downloads    = 0;
    bool adult       = false;
  };

  QString m_domain;
  std::function<void(const QString&)> m_open;
  QLineEdit* m_search;
  QComboBox* m_sort;
  QTreeWidget* m_list;
  QLabel* m_details;
  QPushButton* m_page;
  QLabel* m_status;
  QPushButton* m_more;
  QPushButton* m_openButton;
  int m_request = -1;
  int m_loaded  = 0;
  int m_total   = 0;
  std::vector<Entry> m_entries;
  QNetworkAccessManager m_network;
  QHash<QString, QPixmap> m_images;
  QSet<QString> m_imagesAsked;
  QIcon m_placeholder;

  void search(bool more);
  void onResult(QVariant userData, QVariant result, int id);
  void onFailed(QString, int, int, QVariant, int id, int code, const QString& error);
  void openSelected();
  void showDetails();
  void loadImage(const QString& url);
  QIcon placeholder();
  const Entry* selected() const;
  QString pageOf(const Entry& e) const;
};

#endif  // COLLECTIONBROWSER_H
