
#include "base.hpp"
#include "atomics.hpp"

struct Spin_Lock {
  enum struct Status: u64 { Available = 0, Locked = 1 };

  Atomic<Status> _lock { Status::Available };

  Spin_Lock () = default;

  void lock () {
    using enum Status;
    using enum Memory_Order;

    while (atomic_compare_and_set<Acquire_Release, Acquire>(&_lock, Available, Locked)) {}
  }

  void unlock () {
    atomic_store<Memory_Order::Release>(&_lock, Status::Available);
  }
};

