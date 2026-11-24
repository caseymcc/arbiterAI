#include "processEnd.h"

namespace arbiterAI
{
namespace server
{

bool ProcessEnd::claim()
{
    return !m_claimed.exchange(true);
}

EndAction ProcessEnd::afterStopTimeout()
{
    return claim()?EndAction::Exit:EndAction::None;
}

EndAction ProcessEnd::afterRestartTimeout(bool stopRequested)
{
    if(!claim())
        return EndAction::None;
    return stopRequested?EndAction::Exit:EndAction::Restart;
}

EndAction ProcessEnd::afterListen(bool restartRequested, bool stopRequested)
{
    if(!claim())
        return EndAction::None;
    return restartRequested && !stopRequested?EndAction::Restart:EndAction::Exit;
}

} // namespace server
} // namespace arbiterAI
