#ifndef UPDATECHANNEL_H
#define UPDATECHANNEL_H

// which MO2 Revamped releases a user hears about. Each channel includes the ones
// before it. On GitHub the tag says the channel: "v1.2.0" is stable, "-beta.N"
// and "-rc.N" are betas, "-alpha.N", "-dev.N", "-test.N" and "-preview.N" are
// development builds, early tests that only reach those who chose them. Kept
// apart from selfupdater.h so settings.h does not pull in the network headers.
enum class UpdateChannel
{
  Stable = 0,
  Beta   = 1,
  Dev    = 2
};

#endif  // UPDATECHANNEL_H
