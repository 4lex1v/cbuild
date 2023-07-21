
#include <initializer_list>

#include "arena.hpp"
#include "base.hpp"
#include "result.hpp"
#include "runtime.hpp"

struct Test_Failed_Exception {
  enum Type_Tag {
    Execution,
    General,
    Equality
  };

  Type_Tag tag;

  const char *filename;
  u32         line;
  const char *message;

  String expr;
  String expr_lhs;
  String expr_lhs_value;
  String expr_rhs;
  String expr_rhs_value;
  String details;
};

#define require_success(EXPR) require(capture_status(EXPR) == Status_Code::Success)

#define require(EXPR)                                 \
  if (!static_cast<bool>(EXPR))                                \
    throw Test_Failed_Exception {                     \
      .tag      = Test_Failed_Exception::General,     \
      .filename = __FILE__,                           \
      .line     = static_cast<u32>(__LINE__),         \
      .expr     = #EXPR,                              \
    }                                                 \

#define require_eq(EXPR_LHS, EXPR_RHS)                            \
  {                                                               \
    const auto lhs_value = (EXPR_LHS);              \
    const auto rhs_value = (EXPR_RHS);              \
    if (lhs_value != rhs_value) {                                 \
      throw Test_Failed_Exception {                          \
        .tag      = Test_Failed_Exception::Equality,              \
        .filename = __FILE__,                                     \
        .line     = static_cast<u32>(__LINE__),                   \
        .expr     = stringify(EXPR_LHS) " == " stringify(EXPR_RHS), \
        .expr_lhs = #EXPR_LHS,                                    \
        .expr_lhs_value = to_string(arena, lhs_value),            \
        .expr_rhs = #EXPR_RHS,                                    \
        .expr_rhs_value = to_string(arena, rhs_value),            \
      };                                                          \
    }                                                             \
  }

#define require_lt(EXPR_LHS, EXPR_RHS)                            \
  {                                                               \
    const decltype(EXPR_LHS) lhs_value = (EXPR_LHS);              \
    const decltype(EXPR_RHS) rhs_value = (EXPR_RHS);              \
    if (lhs_value > rhs_value) {                                 \
      throw Test_Failed_Exception {                          \
        .tag      = Test_Failed_Exception::Equality,              \
        .filename = __FILE__,                                     \
        .line     = static_cast<u32>(__LINE__),                   \
        .expr     = stringify(EXPR_LHS) " < " stringify(EXPR_RHS), \
        .expr_lhs = #EXPR_LHS,                                    \
        .expr_lhs_value = std::to_string(lhs_value),              \
        .expr_rhs = #EXPR_RHS,                                    \
        .expr_rhs_value = std::to_string(rhs_value),              \
      };                                                          \
    }                                                             \
  }

struct Test_Case {
  typedef void (*Case_Definition)(Memory_Arena *arena);

  String name;
  Case_Definition case_code;

  void operator () (Memory_Arena *arena) const {
    print(arena, " - %\n", name);

    try {
      case_code(arena);
    }
    catch (const Test_Failed_Exception &error) {
      switch (error.tag) {
        case Test_Failed_Exception::General: {
          print(arena,
                "   Status:\tFailed\n"
                "   Position:\t[%:%]\n"
                "   Expression:\t%\n",
                error.filename, error.line, error.expr);

          break;
        }
        case Test_Failed_Exception::Equality: {
          print(arena,
                "   Status:\tFailed\n"
                "   Position:\t[%:%]\n"
                "   Expression:\t%,\n"
                "\t\twhere\n"
                "\t\t    % = '%'\n"
                "\t\t    % = '%'\n",
                error.filename, error.line, error.expr,
                error.expr_lhs, error.expr_lhs_value,
                error.expr_rhs, error.expr_rhs_value);

          break;
        }
        case Test_Failed_Exception::Execution: {
          print(arena,
                "   Status:\tFailed\n"
                "   Position:\t[%:%]\n"
                "   Details:\t%\n",
                error.filename, error.line,
                error.details);

          break;
        }
      }
      return;
    }

    print(arena, "   Status: Success\n");
  }
};

#define define_test_case(TEST_CASE) \
  Test_Case { .name = #TEST_CASE, .case_code = (TEST_CASE) }

#define define_test_suite(NAME, CASES) \
  void tokenpaste(NAME, _test_suite)(const Test_Suite_Runner &runner) { \
    runner.run_suite(#NAME, CASES);                                     \
  }

struct Test_Suite {
  Test_Case *cases;
  usize      cases_count;
};

template <const usize N>
Test_Suite create_suite (const char *suite_name, const Test_Case (&cases)[N]) {
  return Test_Suite {
  };
}

struct Test_Suite_Runner {
  mutable Memory_Arena arena;

  String suite_filter;
  String case_filter;

  template <const usize N>
  void run_suite (const String &suite_name, const Test_Case (&cases)[N]) const {
    if (suite_filter.is_empty() || compare_strings(suite_filter, suite_name)) {
      print(&this->arena, "Suite: %\n", suite_name);

      for (auto &test_case: cases) {
        if (case_filter.is_empty() || compare_strings(case_filter, test_case.name)) {
          test_case(&arena);
          reset_arena(&arena);
        }
      }
    }
  }
};
