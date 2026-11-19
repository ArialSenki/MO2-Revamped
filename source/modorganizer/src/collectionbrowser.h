#ifndef COLLECTIONBROWSER_H
#define COLLECTIONBROWSER_H

// MO2 Revamped: Nexus collections, from GraphQL collectionsV2, laid out as the
// Wabbajack lists' gallery: search, a game box with how many each game has
// (every game MO2 Revamped has a plugin for), a picture on each row, the chosen
// one's details under the table, and Open sends it to the collection window.
// "All games" shows a section per game with its first collections; one game
// shows all of its own, 20 at a time.

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QIcon>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QSet>
#include <QVariant>
#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace CollectionImages
{
// a collection's picture from Nexus' media server: https and nexusmods.com
// only, at most a few MB; done gets a null image when it can't be read
void fetch(QNetworkAccessManager& network, const QString& url, QObject* context,
           std::function<void(const QImage&)> done);

// a gallery row's picture: scaled into size and centred on a clear box of
// that size, so a square picture and a wide one put the names at one place
// (the Wabbajack gallery's too)
QPixmap thumbnail(const QImage& image, QSize size, qreal dpr);
}  // namespace CollectionImages

class CollectionBrowser : public QDialog
{
  Q_OBJECT

public:
  struct Game
  {
    QString domain;  // Nexus' name for it ("skyrimspecialedition")
    QString name;    // as MO2 shows it
  };

  // games: the ones to list (each once); instanceDomains: the Nexus games this
  // instance takes collections for (the first is its own, chosen at the
  // start); open gets the collection's address
  CollectionBrowser(std::vector<Game> games, const QStringList& instanceDomains,
                    std::function<void(const QString&)> open, QWidget* parent);

private:
  // what a row shows in the details under the table
  struct Entry
  {
    QString slug;
    QString domain;
    QString game;
    QString name;
    QString author;
    QString summary;
    QString category;
    QString image;
    QStringList gameVersions;
    QDateTime updated;
    int revision     = 0;
    int mods         = 0;
    qint64 size      = 0;
    int endorsements = 0;
    int downloads    = 0;
    bool adult       = false;
  };

  std::vector<Game> m_games;
  QStringList m_instanceDomains;
  std::function<void(const QString&)> m_open;
  QLineEdit* m_search;
  QComboBox* m_game;
  QComboBox* m_sort;
  QCheckBox* m_adult;
  QTreeWidget* m_list;
  QLabel* m_details;
  QPushButton* m_page;
  QLabel* m_status;
  QPushButton* m_more;
  QPushButton* m_openButton;
  QTimer* m_typing;
  // the games' counts and their first collections, for every game: in parts
  // (Nexus refuses too large a query), merged as they come
  QSet<int> m_overview;
  QVariantMap m_overviewData;
  QString m_overviewError;
  // one game's collections, page by page
  int m_request = -1;
  int m_loaded  = 0;
  int m_total   = 0;
  QHash<QString, int> m_counts;  // by domain, from the last overview
  std::vector<Entry> m_entries;
  QNetworkAccessManager m_network;
  QHash<QString, QPixmap> m_images;
  QSet<QString> m_imagesAsked;
  QIcon m_placeholder;

  bool allGames() const;
  QString gameName(const QString& domain) const;
  bool takes(const QString& domain) const;
  QJsonObject filterFor(const QString& domain) const;
  void refresh();
  void search(bool more);
  void requestOverview();
  void onResult(QVariant userData, QVariant result, int id);
  void onFailed(QString, int, int, QVariant, int id, int code, const QString& error);
  void readOverview(const QVariantMap& data);
  void readPage(const QVariantMap& data);
  Entry readEntry(const QVariantMap& node) const;
  QTreeWidgetItem* addRow(QTreeWidgetItem* section, Entry e);
  void fillGames();
  void chooseGame(const QString& domain);
  void openSelected();
  void showDetails();
  void loadImage(const QString& url);
  QIcon placeholder();
  const Entry* selected() const;
  QString pageOf(const Entry& e) const;
};

#endif  // COLLECTIONBROWSER_H
