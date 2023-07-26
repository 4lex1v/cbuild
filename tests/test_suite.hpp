
#include <initializer_list>

#include "code/arena.hpp"
#include "code/base.hpp"
#include "code/result.hpp"
#include "code/runtime.hpp"

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
  typedef void (*Case_Step)(Memory_Arena *arena);

  String name;

  Case_Step case_code;
  Case_Step before;
  Case_Step after;
};

#define define_test_case(TEST_CASE) \
  Test_Case { .name = #TEST_CASE, .case_code = (TEST_CASE) }

#define define_test_case_ex(TEST_CASE, BEFORE, AFTER) \
  Test_Case { .name = #TEST_CASE, .case_code = (TEST_CASE), .before = (BEFORE), .after = (AFTER) }

#define define_test_suite(NAME, CASES) \
  void tokenpaste(NAME, _test_suite)(Test_Suite_Runner &runner) { \
    runner.run(#NAME, CASES);                                     \
  }

struct Test_Suite_Runner {
  mutable Memory_Arena arena;

  String suite_filter;
  String case_filter;

  List<String> failed_suites;

  template <const usize N>
  void run (const String &suite_name, const Test_Case (&cases)[N]) {
    if (suite_filter.is_empty() || compare_strings(suite_filter, suite_name)) {
      print(&this->arena, "Suite: %\n", suite_name);

      for (auto &test_case: cases) {
        if (case_filter.is_empty() || compare_strings(case_filter, test_case.name)) {
          enum struct Status {
            Success,
            Setup_Failed,
            Case_Failed,
            Cleanup_Failed,
          };

          Status status = Status::Success;

          {
            auto offset = arena.offset;
            defer { arena.offset = offset; };

            print(&arena, "  - %\n", test_case.name);

            if (test_case.before) {
              try { test_case.before(&arena); }
              catch (const Test_Failed_Exception &error) {
                print(&arena, "    CASE SETUP FAILED\n");
                status = Status::Setup_Failed;
              }
            }

            if (status != Status::Setup_Failed) {
              try { test_case.case_code(&arena); }
              catch (const Test_Failed_Exception &error) {
                status = Status::Case_Failed;
                switch (error.tag) {
                  case Test_Failed_Exception::General: {
                    print(&arena,
                          "   Status:\tFailed\n"
                          "   Position:\t[%:%]\n"
                          "   Expression:\t%\n",
                          error.filename, error.line, error.expr);

                    break;
                  }
                  case Test_Failed_Exception::Equality: {
                    print(&arena,
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
                    print(&arena,
                          "   Status:\tFailed\n"
                          "   Position:\t[%:%]\n"
                          "   Details:\t%\n",
                          error.filename, error.line,
                          error.details);

                    break;
                  }
                }
              }
            }

            if (status != Status::Setup_Failed && test_case.after) {
              try { test_case.after(&arena); }
              catch (const Test_Failed_Exception &error) {
                print(&arena, "    CASE CLEANUP FAILED\n");
                status = Status::Cleanup_Failed;
              }
            }
          }

          if (status != Status::Success)
            add(&arena, &failed_suites, copy_string(&arena, test_case.name));
        }
      }
    }
  }

  int report () const {
    if (failed_suites.count == 0) return 0;

    print(&arena, "Failed: ");
    for (auto &name: failed_suites) print(&arena, "%, ", name);
    print(&arena, "\n");

    return 1;
  }
};
