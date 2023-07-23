
#pragma once

#include "base.hpp"
#include "arena.hpp"

template <typename T>
struct List {
  using Value_Type = T;
  
  struct Node {
    Value_Type  value;
    Node       *next;
    Node       *previous;
  };

  Node  *first = nullptr;
  Node  *last  = nullptr;

  usize count = 0;

  struct List_Iterator {
    Node *node;

    List_Iterator (Node *_node): node { _node } {}

    bool operator != (const List_Iterator &other) const {
      return this->node != other.node;
    }

    List_Iterator& operator ++ () {
      node = node->next;
      return *this;
    }

    Value_Type&       operator * ()       { return node->value; }
    const Value_Type& operator * () const { return node->value; }
  };

  List_Iterator begin () { return List_Iterator(first); } 
  List_Iterator end   () { return List_Iterator(nullptr); }

  const List_Iterator begin () const { return List_Iterator(first); } 
  const List_Iterator end   () const { return List_Iterator(nullptr); }
};

template <typename T>
using List_Value = typename List<T>::Value_Type;

template <typename T>
static void add (Memory_Arena *arena, List<T> *list, List_Value<T> value) {
  auto node = push_struct<typename List<T>::Node>(arena, value);

  if (list->first == nullptr) {
    list->first = list->last = node;
  }
  else {
    node->previous   = list->last;
    list->last->next = node;

    list->last       = node;
  }

  list->count += 1;
}

template <typename T, typename P>
static Pair<bool, usize> find_position (const List<T> *list, P predicate) {
  auto node = list->first;
  for (usize idx = 0; idx < list->count; ++idx) {
    if (predicate(&(node->value))) return { true, idx };
    node = node->next;
  }

  return { false, 0 };
}

template <typename T>
static bool remove_at (List<T> *list, usize position) {
  if (list->count == 0 || position >= list->count) return false;

  auto node = list->first;
  for (usize i = 0; i < position; ++i) node = node->next;

  if (node->previous != nullptr) node->previous->next = node->next;
  else                           list->first = node->next;

  if (node->next != nullptr) node->next->previous = node->previous;
  else                       list->last = node->previous;

  list->count--;

  return true;
}

template <typename T>
static bool is_empty_list (const List<T> *list) {
  return list->first == nullptr;
}

