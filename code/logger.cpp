
#include "anyfin/concurrent.hpp"
#include "anyfin/console.hpp"

/*
  TODO: This is a temporary solution to get multi-threaded logging going. CBuild doesn't log much, there shouldn't
  be much contention over the lock. 
 */

static Fin::Spin_Lock log_lock;

void log (Fin::String message) {
  log_lock.lock();
  write_to_stdout(message);
  log_lock.unlock();
}
