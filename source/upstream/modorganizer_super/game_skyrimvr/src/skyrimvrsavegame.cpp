#include "skyrimvrsavegame.h"

#include <Windows.h>

#include "gameskyrimvr.h"

SkyrimVRSaveGame::SkyrimVRSaveGame(QString const& fileName, GameSkyrimVR const* game)
    : GamebryoSaveGame(fileName, game, true)
{
  FileWrapper file(fileName, "TESV_SAVEGAME");  // 10bytes

  unsigned long version;
  FILETIME ftime;
  fetchInformationFields(file, version, m_PCName, m_PCLevel, m_PCLocation, m_SaveNumber,
                         ftime);

  // A file time is a 64-bit value that represents the number of 100-nanosecond
  // intervals that have elapsed since 12:00 A.M. January 1, 1601 Coordinated Universal
  // Time (UTC). So we need to convert that to something useful

  // MO2 Revamped: it is UTC, as in Skyrim SE (FallrimTools' ReSaver reads it
  // so); the 6 hours taken off here made up for the Saves tab showing UTC

  SYSTEMTIME ctime;
  ::FileTimeToSystemTime(&ftime, &ctime);

  setCreationTime(ctime);
}

void SkyrimVRSaveGame::fetchInformationFields(FileWrapper& file, unsigned long& version,
                                              QString& playerName,
                                              unsigned short& playerLevel,
                                              QString& playerLocation,
                                              unsigned long& saveNumber,
                                              FILETIME& creationTime) const
{
  unsigned long headerSize;
  file.read(headerSize);  // header size "TESV_SAVEGAME"
  file.read(version);     // header version 74 (original Skyrim is 79)
  file.read(saveNumber);

  file.read(playerName);

  unsigned long temp;
  file.read(temp);
  playerLevel = static_cast<unsigned short>(temp);

  file.read(playerLocation);

  QString timeOfDay;
  file.read(timeOfDay);

  QString race;
  file.read(race);  // race name (i.e. BretonRace)

  file.skip<unsigned short>();  // Player gender (0 = male)
  file.skip<float>(2);          // experience gathered, experience required

  file.read(creationTime);  // filetime
}

std::unique_ptr<GamebryoSaveGame::DataFields> SkyrimVRSaveGame::fetchDataFields() const
{
  FileWrapper file(getFilepath(), "TESV_SAVEGAME");  // 10bytes

  unsigned long version = 0;
  {
    QString dummyName, dummyLocation;
    unsigned short dummyLevel;
    unsigned long dummySaveNumber;
    FILETIME dummyTime;

    fetchInformationFields(file, version, dummyName, dummyLevel, dummyLocation,
                           dummySaveNumber, dummyTime);
  }

  std::unique_ptr<DataFields> fields = std::make_unique<DataFields>();

  unsigned long width;
  unsigned long height;
  file.read(width);
  file.read(height);

  uint16_t compressionType;
  file.read(compressionType);
  file.setCompressionType(compressionType);

  fields->Screenshot = file.readImage(width, height, 320, true);

  file.openCompressedData();

  uint8_t saveGameVersion = file.readChar();
  uint8_t pluginInfoSize  = file.readChar();
  uint16_t other          = file.readShort();  // Unknown

  fields->Plugins = file.readPlugins(1);  // Just empty data

  if (saveGameVersion >= 78) {
    fields->LightPlugins = file.readLightPlugins();
  }

  file.closeCompressedData();

  return fields;
}
