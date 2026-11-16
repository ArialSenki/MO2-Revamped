#ifndef WABBAJACKBROWSER_H
#define WABBAJACKBROWSER_H

// MO2 Revamped: Wabbajack's gallery of modlists, as its own app shows it: every
// repository's lists, by game, with search. Install downloads the list's
// .wabbajack from Wabbajack's servers (in parts, each checked) and hands it to
// the list's window, where its archives download and it installs.

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

namespace MOBase
{
class IPluginGame;
}

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QFile;
class QLabel;
class QLineEdit;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace Wabbajack
{

class ListBrowser : public QDialog
{
  Q_OBJECT

public:
  // open gets the downloaded .wabbajack's path
  // the lists' .wabbajack files go to a folder in downloadsFolder
  // inUse tells whether a list file is open or kept to resume: it is never
  // replaced then
  ListBrowser(std::vector<MOBase::IPluginGame*> games,
              const MOBase::IPluginGame* managedGame, const QString& downloadsFolder,
              std::function<void(const QString&)> open,
              std::function<bool(const QString&)> inUse, QWidget* parent);
  ~ListBrowser() override;

protected:
  void closeEvent(QCloseEvent* e) override;

private:
  struct Entry
  {
    QString title, description, author, game, version, repository, machine;
    QString image, smallImage, readme, website, discord, download, hash;
    QStringList tags;
    qint64 size = 0, downloadSize = 0, installSize = 0;
    int archives = 0;
    bool nsfw = false, official = false, utility = false, down = false,
         failing = false;
    QDateTime updated;
  };

  std::vector<MOBase::IPluginGame*> m_games;
  const MOBase::IPluginGame* m_managed;
  std::function<void(const QString&)> m_open;
  std::function<bool(const QString&)> m_inUse;
  // the check of a list file already here (2: no second click meanwhile)
  QPointer<QObject> m_checking;
  QString m_listsFolder;
  QNetworkAccessManager m_network;
  std::vector<Entry> m_entries;
  QHash<QString, QPixmap> m_images;  // small images by address
  QSet<QString> m_imagesAsked;

  QLineEdit* m_search;
  QComboBox* m_game;
  QCheckBox* m_unofficial;
  QCheckBox* m_nsfw;
  QTreeWidget* m_list;
  QLabel* m_details;
  QPushButton* m_readme;
  QPushButton* m_website;
  QPushButton* m_discord;
  QLabel* m_status;
  QProgressBar* m_progress;
  QPushButton* m_refresh;
  QPushButton* m_install;
  QPushButton* m_close;

  // the gallery being read: the repositories still to answer
  int m_pending = 0;
  QJsonArray m_collected;  // every repository's lists, with its name
  QByteArray m_summary;    // the lists' status and small images
  int m_generation = 0;    // a reload makes the older answers stale
  int m_failed     = 0;    // repositories that didn't answer in this reading
  QString m_indexError;    // why the index itself couldn't be read

  // the list's file being downloaded
  struct Download;
  std::unique_ptr<Download> m_download;

  void setupUi();
  void loadGallery(bool fresh);
  void readGallery(const QJsonArray& lists, const QByteArray& summary);
  void galleryDone(int generation);
  void fillGames();
  void fillList();
  void showDetails();
  const Entry* selected() const;
  void loadImage(QTreeWidgetItem* item, const QString& url);

  void install();
  void startDownload(const Entry& e, const QString& target);
  void nextPart();
  void finishDownload(bool ok, const QString& error, bool keepPart = false);
  void updateButtons();
};

}  // namespace Wabbajack

#endif  // WABBAJACKBROWSER_H
