#ifndef _ARBITERAI_SERVER_PROCESSEND_H_
#define _ARBITERAI_SERVER_PROCESSEND_H_

#include <atomic>

namespace arbiterAI
{
namespace server
{

enum class EndAction
{
    None,   ///< another path already ends the process: leave it alone
    Exit,
    Restart ///< execv the new install
};

/// Three paths end the server process: the update's restart worker and the
/// stop handler, each once its drain timed out and listen() did not return,
/// and main once listen() returned. Whichever asks first ends it.
class ProcessEnd {
public:
    /// The stop handler, its drain timed out.
    EndAction afterStopTimeout();
    /// The restart worker, its drain timed out. A stop signal wins over the
    /// restart: the supervisor is stopping the service, and starts the new
    /// install from disk the next time.
    EndAction afterRestartTimeout(bool stopRequested);
    /// main, listen() returned.
    EndAction afterListen(bool restartRequested, bool stopRequested);

    /// The restart worker and the stop handler both stop the listener; true
    /// for the first. Once listen() returned, wait_until_ready() spins for
    /// ever, and a second stop() trips httplib's assert.
    bool claimListenerStop();

private:
    bool claim();

    std::atomic<bool> m_claimed{false};
    std::atomic<bool> m_listenerStopClaimed{false};
};

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_PROCESSEND_H_
