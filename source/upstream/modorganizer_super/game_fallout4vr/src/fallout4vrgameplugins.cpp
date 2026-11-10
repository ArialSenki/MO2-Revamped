#include "fallout4vrgameplugins.h"

using namespace MOBase;

Fallout4VRGamePlugins::Fallout4VRGamePlugins(MOBase::IOrganizer* organizer)
    : CreationGamePlugins(organizer)
{}

bool Fallout4VRGamePlugins::lightPluginsAreSupported()
{
  // MO2 Revamped: from MO2 afe4d4d, 242a772
  auto files =
      m_Organizer->findFiles("f4se\\plugins", {"falloutvresl.dll", "Daytripper4.dll"});
  if (files.isEmpty())
    return false;
  return true;
}
