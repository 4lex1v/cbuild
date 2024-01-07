
#include "anyfin/base.hpp"

#include "anyfin/core/arena.hpp"
#include "anyfin/core/result.hpp"
#include "anyfin/core/callsite.hpp"
#include "anyfin/core/meta.hpp"

#include "anyfin/platform/commands.hpp"
#include "anyfin/platform/file_system.hpp"
#include "anyfin/platform/console.hpp"

using namespace Fin::Core;
using namespace Fin::Platform;

struct Test_Failed_Exception {
  enum Type_Tag { Execution, General, Equality };

  Type_Tag tag;

  Callsite_Info callsite;
  String_View   message;

  String_View expr;
  String_View expr_lhs;
  String_View expr_lhs_value;
  String_View expr_rhs;
  String_View expr_rhs_value;
  String_View details;
};

#define require_success(EXPR) require(capture_status(EXPR) == Status_Code::Success)

#define require(EXPR) //require_impl(static_cast<bool>(EXPR), #EXPR)
// static void require_impl (bool result, String_View expr, Callsite_Info callsite = {}) {
//   if (!result)
//     throw Test_Failed_Exception {                 
//       .tag      = Test_Failed_Exception::General, 
//       .callsite = callsite,
//       .expr     = expr
//     };
// }

#define require_eq(EXPR_LHS, EXPR_RHS)
// #define require_eq(EXPR_LHS, EXPR_RHS)                              \
//   {                                                                 \
//     const auto lhs_value = (EXPR_LHS);                              \
//     const auto rhs_value = (EXPR_RHS);                              \
//     if (lhs_value != rhs_value) {                                   \
//       throw Test_Failed_Exception {                                 \
//         .tag      = Test_Failed_Exception::Equality,                \
//         .callsite = Callsite_Info(),                                \
//         .expr     = stringify(EXPR_LHS) " == " stringify(EXPR_RHS), \
//         .expr_lhs = #EXPR_LHS,                                      \
//         .expr_lhs_value = to_string(arena, lhs_value),              \
//         .expr_rhs = #EXPR_RHS,                                      \
//         .expr_rhs_value = to_string(arena, rhs_value),              \
//       };                                                            \
//     }                                                               \
//   }

#define require_lt(EXPR_LHS, EXPR_RHS)
// #define require_lt(EXPR_LHS, EXPR_RHS)                            \
//   {                                                               \
//     const decltype(EXPR_LHS) lhs_value = (EXPR_LHS);              \
//     const decltype(EXPR_RHS) rhs_value = (EXPR_RHS);              \
//     if (lhs_value > rhs_value) {                                 \
//       throw Test_Failed_Exception {                          \
//         .tag      = Test_Failed_Exception::Equality,              \
//         .filename = __FILE__,                                     \
//         .line     = static_cast<u32>(__LINE__),                   \
//         .expr     = stringify(EXPR_LHS) " < " stringify(EXPR_RHS), \
//         .expr_lhs = #EXPR_LHS,                                    \
//         .expr_lhs_value = std::to_string(lhs_value),              \
//         .expr_rhs = #EXPR_RHS,                                    \
//         .expr_rhs_value = std::to_string(rhs_value),              \
//       };                                                          \
//     }                                                             \
//   }

struct Test_Case {
  typedef void (*Case_Step)(Memory_Arena &arena);

  String_View name;

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

  String_View suite_filter;
  String_View case_filter;

  List<String_View> failed_suites;

  template <const usize N>
  void run (const String_View &suite_name, const Test_Case (&cases)[N]) {
    //__builtin_setjmp(nullptr);
    // if (is_empty(suite_filter) || compare_strings(suite_filter, suite_name)) {
    //   print("Suite: %\n", suite_name);

    //   for (auto &test_case: cases) {
    //     if (is_empty(case_filter) || compare_strings(case_filter, test_case.name)) {
    //       enum struct Status {
    //         Success,
    //         Setup_Failed,
    //         Case_Failed,
    //         Cleanup_Failed,
    //       };

    //       Status status = Status::Success;

    //       {
    //         auto offset = arena.offset;
    //         defer { arena.offset = offset; };

    //         print("  - %\n", test_case.name);

    //         if (test_case.before) {
    //           try { test_case.before(arena); }
    //           catch (const Test_Failed_Exception &error) {
    //             print("    CASE SETUP FAILED\n");
    //             status = Status::Setup_Failed;
    //           }
    //         }

    //         if (status != Status::Setup_Failed) {
    //           try { test_case.case_code(arena); }
    //           catch (const Test_Failed_Exception &error) {
    //             status = Status::Case_Failed;

    //             auto filename = String_View(error.callsite.file);
    //             auto function = String_View(error.callsite.function);
    //             auto line     = error.callsite.line;

    //             switch (error.tag) {
    //               case Test_Failed_Exception::General: {
    //                 print("   Status:\tFailed\n"
    //                       "   Position:\t[%:%]\n"
    //                       "   Expression:\t%\n",
    //                       filename, line, error.expr);

    //                 break;
    //               }
    //               case Test_Failed_Exception::Equality: {
    //                 print("   Status:\tFailed\n"
    //                       "   Position:\t[%:%]\n"
    //                       "   Expression:\t%,\n"
    //                       "\t\twhere\n"
    //                       "\t\t    % = '%'\n"
    //                       "\t\t    % = '%'\n",
    //                       filename, line, error.expr,
    //                       error.expr_lhs, error.expr_lhs_value,
    //                       error.expr_rhs, error.expr_rhs_value);

    //                 break;
    //               }
    //               case Test_Failed_Exception::Execution: {
    //                 print("   Status:\tFailed\n"
    //                       "   Position:\t[%:%]\n"
    //                       "   Details:\t%\n",
    //                       filename, line, error.details);

    //                 break;
    //               }
    //             }
    //           }
    //         }

    //         if (status != Status::Setup_Failed && test_case.after) {
    //           try { test_case.after(arena); }
    //           catch (const Test_Failed_Exception &error) {
    //             print("    CASE CLEANUP FAILED\n");
    //             status = Status::Cleanup_Failed;
    //           }
    //         }
    //       }

    //       if (status != Status::Success) list_push_copy(failed_suites, test_case.name);
    //     }
    //   }
    // }
  }

  int report () const {
    if (failed_suites.count == 0) {
      print("\nSUCCESS");
      return 0;
    }

    print("\n\nFAILED (%): ", failed_suites.count);
    for (auto &name: failed_suites) print("%, ", name);
    print("\n");

    return 1;
  }
};

