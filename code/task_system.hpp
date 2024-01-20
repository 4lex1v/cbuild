
#pragma once

#include "anyfin/core/allocator.hpp"
#include "anyfin/core/atomics.hpp"
#include "anyfin/core/arrays.hpp"
#include "anyfin/core/list.hpp"
#include "anyfin/core/result.hpp"

#include "anyfin/platform/concurrent.hpp"
#include "anyfin/platform/platform.hpp"
#include "anyfin/platform/threads.hpp"

#include "cbuild.hpp"

template <typename T>
struct Task_Queue {
  struct Node {
    static_assert(sizeof(T) <= CACHE_LINE_SIZE - sizeof(as32));
    
    T    task;
    as32 sequence_number;

    char cache_line_pad[CACHE_LINE_SIZE - sizeof(T) - sizeof(as32)];
  };

  static_assert(sizeof(Node) == CACHE_LINE_SIZE);

  Allocator_View allocator;

  Array<Node> tasks_queue;

  cas64 write_index = 0;
  cas64 read_index  = 0;

  cau32 tasks_submitted = 0;
  cau32 tasks_completed = 0;

  Task_Queue (Allocator auto &_allocator, const usize queue_size)
    : allocator   { _allocator },
      tasks_queue { reserve_array<Node>(allocator, align_forward_to_pow_2(queue_size)) }
  {
    for (s32 idx = 0; auto &node: tasks_queue) {
      node.sequence_number = idx++;
    }
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
        if (atomic_compare_and_set(this->write_index, index, index + 1)) break;
      }
      else if (diff < 0) return false;
      else index = atomic_load(this->write_index);
    }

    /*
      The submitted count is only checked to see if there are unfinished tasks in the queue or not,
      so we want to increment it as early as possible.
    */
    atomic_fetch_add(this->tasks_submitted, 1);

    zero_memory(&node->task);
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

template <typename T>
static void destroy (Task_Queue<T> &queue) {
  destroy(queue.tasks_queue);
}

/*
  Task system is designed to support concurrent execution with ability to execute tasks on the main thread as well, avoiding
  wasted CPU potential.
*/
template <typename T, typename BC>
struct Task_System {
  using Queue   = Task_Queue<T>;
  using Handler = void (*) (Task_System<T, BC> &, BC &, T &);

  Queue         queue;
  Array<Thread> builders {};
  Semaphore     semaphore;

  Handler func;

  abool terminating = false;

  Task_System (Allocator auto &allocator, const usize queue_size, const usize builders_count, Handler &&_func)
    : queue     { allocator, queue_size },
      semaphore { create_semaphore().take("Failed to create a semaphore resource for the build queue system") },
      func      { _func }
  {
    if (builders_count) {
      builders = reserve_array<Thread>(allocator, builders_count);
      for (auto &builder: builders) builder = *spawn_thread(task_system_loop, this); 
    }
  }

  static void task_system_loop (Task_System<T, BC> *system) {
    BC context {};

    while (true) {
      wait_for_semaphore_signal(system->semaphore);

      if (atomic_load<Memory_Order::Acquire>(system->terminating)) break;

      system->execute_task(context);
    }
  }

  bool has_unfinished_tasks () {
    auto completed = atomic_load(this->queue.tasks_completed);
    auto submitted = atomic_load(this->queue.tasks_submitted);

    assert(completed <= submitted);

    return (submitted != completed);
  }

  /*
    `execute_task` could be called by the main thread and executed on the main thread respectively.
  */
  void execute_task (BC &context) {
    auto option = this->queue.pop_task();
    if (!option) return;

    auto task = option.take();

    this->func(*this, context, task);

    // TODO: This feels slightly out of place
    atomic_fetch_add(queue.tasks_completed, 1);
  }

  void add_task (T &&task) {
    this->queue.push_task(move(task));

    increment_semaphore(this->semaphore);
  }
};

template <typename T, typename BC>
static void destroy (Task_System<T, BC> &system) {
  atomic_store<Memory_Order::Release>(system.terminating, true);
    
  // TODO: This shows that current approach has some flaws as the task system shouldn't do this
  // to unblock threads, either it should own the semaphore, which transitively means that the
  // queue shouldn't be a separate entity.
  increment_semaphore(system.semaphore, system.builders.count);

  destroy(system.semaphore);
  destroy(system.queue);
}

