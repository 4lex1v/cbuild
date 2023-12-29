
#pragma once

#include "anyfin/core/allocator.hpp"
#include "anyfin/core/atomics.hpp"
#include "anyfin/core/arrays.hpp"
#include "anyfin/core/list.hpp"
#include "anyfin/core/result.hpp"
#include "anyfin/core/seq.hpp"

#include "anyfin/platform/files.hpp"
#include "anyfin/platform/concurrent.hpp"
#include "anyfin/platform/platform.hpp"
#include "anyfin/platform/threads.hpp"

#include "cbuild.hpp"

template <typename T>
struct Build_Queue {
  struct Node {
    static_assert(sizeof(T) <= CACHE_LINE_SIZE - sizeof(as32));
    
    T    task;
    as32 sequence_number;

    char cache_line_pad[CACHE_LINE_SIZE - sizeof(T) - sizeof(as32)];
  };

  static_assert(sizeof(Node) == CACHE_LINE_SIZE);

  Allocator allocator;

  Slice<Node> tasks_queue;

  cas64 write_index = 0;
  cas64 read_index  = 0;

  cau32 tasks_submitted = 0;
  cau32 tasks_completed = 0;

  Build_Queue (Allocator &_allocator, const usize queue_size)
    : allocator       { _allocator },
      tasks_queue     { reserve_array<Node>(allocator, align_forward_to_pow_2(queue_size)) }
    {}

  ~Build_Queue () {
    for (auto &node: this->tasks_queue) node.task.~T();
    free_memory(this->allocator, this->tasks_queue.elements);
  }

  bool push_task (T &&task) {
    using enum Memory_Order;
  
    auto index = atomic_load(this->write_index);

    const auto tasks_count = this->tasks_queue.count;
    const auto mask        = tasks_count - 1;

    Node *node = nullptr;
    while (true) {
      node = &this->tasks_queue[index & mask];

      auto sequence = atomic_load<Acquire>(node->sequence_number);
      auto diff     = sequence - index;
    
      if (diff == 0) {
        if (atomic_compare_and_set<Whatever, Whatever>(this->write_index, index, index + 1)) break;
      }
      else if (diff < 0) return false;
      else index = atomic_load(this->write_index);
    }

    atomic_fetch_add(this->tasks_submitted, 1);

    node->task = move(task);
    atomic_store<Release>(node->sequence_number, index + 1);


    return true;
  }

  Option<T> pop_task () {
    using enum Memory_Order;
  
    auto index = atomic_load(this->read_index);

    const auto tasks_count = this->tasks_queue.count;
    const auto mask        = tasks_count - 1;

    Node *node = nullptr;
    while (true) {
      node = &this->tasks_queue[index & mask];

      auto sequence = atomic_load<Acquire>(node->sequence_number);
      auto diff     = sequence - (index + 1);

      if (diff == 0) {
        if (atomic_compare_and_set<Whatever, Whatever>(this->read_index, index, index + 1)) break;
      }
      else if (diff < 0) return {};
      else index = atomic_load(this->read_index);
    }

    auto task = move(node->task);

    atomic_store<Release>(node->sequence_number, index + tasks_count);

    return Option(move(task));
  }
};

/*
  Task system is designed to support concurrent execution with ability to execute tasks on the main thread as well, avoiding
  wasted CPU potential.
*/
template <typename T, typename BC>
struct Task_System {
  using Queue   = Build_Queue<T>;
  using Handler = void (*) (BC &, T &);

  Queue         queue;
  Slice<Thread> builders;
  Semaphore     semaphore;

  Handler handler;

  abool terminating = false;

  Task_System (Allocator allocator, const usize queue_size, const usize builders_count, Handler &&func)
    : queue     { allocator, queue_size },
      builders  { reserve_array<Thread>(allocator, builders_count) },
      semaphore { create_semaphore().take("Failed to create a semaphore resource for the build queue system") },
      handler   { func }
    {
      for (auto &builder: builders) {
        builder = *spawn_thread([this] () {
          BC context {};

          while (true) {
            wait_for_semaphore_signal(queue.tasks_available);

            if (atomic_load<Memory_Order::Acquire>(this->terminating)) break;

            this->execute_task(context);
          }
        }); 
      }
    }

  ~Task_System () {
    destroy_semaphore(this->tasks_available);

    atomic_store<Memory_Order::Release>(this->terminating, true);
    
    // TODO: This shows that current approach has some flaws as the task system shouldn't do this
    // to unblock threads, either it should own the semaphore, which transitively means that the
    // queue shouldn't be a separate entity.
    increment_semaphore(this->semaphore, this->builders.count);

    this->queue.~Queue();
  }

  bool has_unfinished_tasks () {
    auto submitted = atomic_load(this->semaphore);
    auto completed = atomic_load(this->semaphore);

    assert(submitted <= completed);

    return (submitted != completed);
  }

  /*
    `execute_task` could be called by the main thread and executed on the main thread respectively.
  */
  void execute_task (BC &context) {
    pop_task(this->queue).handle_value([&, this] (auto task) {
      this->handle(context, task);

      // TODO: This feels slightly out of place
      atomic_fetch_add(queue.tasks_completed, 1);
    });
  }

  void add_task (T &&task) {
    this->queue.push(move(task));

    increment_semaphore(this->semaphore);
  }
};



