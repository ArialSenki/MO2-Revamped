#include "morrowindsavegameinfowidget.h"
#include "savegamecard.h"

// MO2 Revamped: built in code instead of from 2.5.2's .ui, as a card; the
// header, and so the game plugins built on it, stay as they were
namespace Ui
{
class MorrowindSaveGameInfoWidget
{
public:
  RevampedFluent::SaveGameCard card;

  void setupUi(QWidget* widget)
  {
    widget->setObjectName(QStringLiteral("MorrowindSaveGameInfoWidget"));
    card.setup(widget);
  }
};
}  // namespace Ui

#include "gamemorrowind.h"
#include "imoinfo.h"
#include "ipluginlist.h"
#include "morrowindsavegame.h"
#include "morrowindsavegameinfo.h"

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

MorrowindSaveGameInfoWidget::MorrowindSaveGameInfoWidget(
    MorrowindSaveGameInfo const* info, QWidget* parent)
    : MOBase::ISaveGameInfoWidget(parent), ui(new Ui::MorrowindSaveGameInfoWidget),
      m_Info(info)
{
  ui->setupUi(this);
}

MorrowindSaveGameInfoWidget::~MorrowindSaveGameInfoWidget()
{
  delete ui;
}

void MorrowindSaveGameInfoWidget::setSave(MOBase::ISaveGame const& save)
{
  auto const& morrowindSave = dynamic_cast<MorrowindSaveGame const&>(save);
  auto& card                = ui->card;

  card.setScreenshot(morrowindSave.getScreenshot());
  card.setTitle(morrowindSave.getPCName());
  const QDateTime t = morrowindSave.getCreationTime();
  card.setLines(
      {morrowindSave.getPCLocation().isEmpty()
           ? tr("Level %1").arg(morrowindSave.getPCLevel())
           : tr("Level %1, %2").arg(QString::number(morrowindSave.getPCLevel()), morrowindSave.getPCLocation()),
       tr("Health %1 of %2").arg(qRound(morrowindSave.getPCCurrentHealth())).arg(qRound(morrowindSave.getPCMaxHealth())),
       tr("%1, day %2").arg(morrowindSave.getSaveName(), QString::number(qRound(morrowindSave.getGameDays()))),
       tr("Save %1, %2").arg(morrowindSave.getSaveNumber()).arg(QLocale::system().toString(t.date(), QLocale::FormatType::ShortFormat) + " " +
      QLocale::system().toString(t.time(), QLocale::FormatType::ShortFormat))});

  card.clearSections();
  MOBase::IPluginList* pluginList = m_Info->m_Game->m_Organizer->pluginList();
  QStringList missing;
  for (QString const& pluginName : morrowindSave.getPlugins()) {
    if (pluginList->state(pluginName) != MOBase::IPluginList::STATE_ACTIVE) {
      missing.append(pluginName);
    }
  }
  card.addMissing(tr("Missing plugins"), missing);
  card.finish();
}
