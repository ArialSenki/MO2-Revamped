#ifndef FLUENTPERF_H
#define FLUENTPERF_H

// Timing probes for the UI, active only when MO2_REVAMPED_PERF is set; they
// write "[perf]" lines to the log. Off, a probe costs one cached bool check.

#include <QElapsedTimer>
#include <QString>
#include <log.h>

namespace FluentPerf
{

inline bool enabled()
{
  static const bool on = qEnvironmentVariableIsSet("MO2_REVAMPED_PERF");
  return on;
}

// logs the time of each step of a piece of work, and the total
class Timer
{
public:
  explicit Timer(const char* what) : m_what(what)
  {
    if (enabled()) {
      m_clock.start();
    }
  }

  void lap(const char* step)
  {
    if (!enabled()) {
      return;
    }
    const qint64 now = m_clock.nsecsElapsed();
    MOBase::log::info("[perf] {} / {}: {:.1f} ms", m_what, step,
                      (now - m_last) / 1000000.0);
    m_last = now;
  }

  ~Timer()
  {
    if (enabled()) {
      MOBase::log::info("[perf] {}: {:.1f} ms total", m_what,
                        m_clock.nsecsElapsed() / 1000000.0);
    }
  }

private:
  const char* m_what;
  QElapsedTimer m_clock;
  qint64 m_last = 0;
};

}  // namespace FluentPerf

#endif  // FLUENTPERF_H
