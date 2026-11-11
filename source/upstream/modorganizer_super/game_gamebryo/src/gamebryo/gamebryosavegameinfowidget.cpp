#include "gamebryosavegameinfowidget.h"
#include "savegamecard.h"

// MO2 Revamped: built in code instead of from 2.5.2's .ui, as a card; the
// header, and so the game plugins built on it, stay as they were
namespace Ui
{
class GamebryoSaveGameInfoWidget
{
public:
  RevampedFluent::SaveGameCard card;

  void setupUi(QWidget* widget)
  {
    widget->setObjectName(QStringLiteral("GamebryoSaveGameInfoWidget"));
    card.setup(widget);
  }
};
}  // namespace Ui

#include "gamebryosavegame.h"
#include "gamebryosavegameinfo.h"
#include "gamegamebryo.h"
#include "imoinfo.h"
#include "ipluginlist.h"

#include <QDate>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QPixmap>
#include <QString>
#include <QStyle>
#include <QTime>
#include <QVBoxLayout>

#include <Qt>
#include <QtGlobal>

#include <memory>

GamebryoSaveGameInfoWidget::GamebryoSaveGameInfoWidget(GamebryoSaveGameInfo const* info,
                                                       QWidget* parent)
    : MOBase::ISaveGameInfoWidget(parent), ui(new Ui::GamebryoSaveGameInfoWidget),
      m_Info(info)
{
  ui->setupUi(this);
}

GamebryoSaveGameInfoWidget::~GamebryoSaveGameInfoWidget()
{
  delete ui;
}

void GamebryoSaveGameInfoWidget::setSave(MOBase::ISaveGame const& save)
{
  auto& gamebryoSave = dynamic_cast<GamebryoSaveGame const&>(save);
  auto& card         = ui->card;

  card.setScreenshot(gamebryoSave.getScreenshot());
  card.setTitle(gamebryoSave.getPCName());
  const QDateTime t = gamebryoSave.getCreationTime().toLocalTime();
  const QString location = gamebryoSave.getPCLocation();
  card.setLines({location.isEmpty()
                     ? tr("Level %1").arg(gamebryoSave.getPCLevel())
                     : tr("Level %1, %2").arg(QString::number(gamebryoSave.getPCLevel()), location),
                 tr("Save %1, %2").arg(gamebryoSave.getSaveNumber()).arg(QLocale::system().toString(t.date(), QLocale::FormatType::ShortFormat) + " " +
      QLocale::system().toString(t.time(), QLocale::FormatType::ShortFormat))});

  card.clearSections();
  if (gamebryoSave.hasScriptExtenderFile()) {
    card.addNote(tr("Has script extender data"));
  }
  // the save's plugins that aren't active, by kind
  MOBase::IPluginList* pluginList = m_Info->m_Game->m_Organizer->pluginList();
  auto missing = [pluginList](const QStringList& plugins) {
    QStringList out;
    for (QString const& pluginName : plugins) {
      if (pluginList->state(pluginName) != MOBase::IPluginList::STATE_ACTIVE) {
        out.append(pluginName);
      }
    }
    return out;
  };
  card.addMissing(tr("Missing plugins (ESP, ESM)"), missing(gamebryoSave.getPlugins()));
  if (gamebryoSave.isMediumEnabled()) {
    card.addMissing(tr("Missing medium plugins (ESH)"), missing(gamebryoSave.getMediumPlugins()));
  }
  if (gamebryoSave.isLightEnabled()) {
    card.addMissing(tr("Missing light plugins (ESL)"), missing(gamebryoSave.getLightPlugins()));
  }
  card.finish();
}
