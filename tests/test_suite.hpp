
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

struct Case_Run_Result {
  bool          error;
  Callsite_Info callsite;
  String_View   message;

  // String_View expr;
  // String_View expr_lhs;
  // String_View expr_lhs_value;
  // String_View expr_rhs;
  // String_View expr_rhs_value;
  // String_View details;
};

#define require_eq(EXPR_LHS, EXPR_RHS)
#define require_lt(EXPR_LHS, EXPR_RHS)

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

/*
  TODO:
    I'm not happy with this approach at all, but at this point don't have much time to iterate on this design,
    need to focus on getting these tests up and running to continue with restoring cbuild's main functionality.
    After that I'll refine the API to facilitate a better architecture and separation between the runner and
    test cases.
 */
static void *context[16];
static Case_Run_Result result;

template <typename S, typename T>
static void require (const Fin::Core::Result<S, T> &_result, Callsite_Info _callsite = {}) {
  if (!_result) {
    print("Failed result value: %\n", _result.status);

    result = Case_Run_Result {
      .error    = true,
      .callsite = _callsite
    };
    
    __builtin_longjmp(context, 1);
  }
}

static void require (bool check, Callsite_Info _callsite = {}) {
  if (!check) {
    result = Case_Run_Result {
      .error    = true,
      .callsite = _callsite
    };
    
    __builtin_longjmp(context, 1);
  }
}

struct Test_Suite_Runner {
  enum struct Status: u32 { Success, Setup_Failed, Case_Failed, Cleanup_Failed };

  mutable Memory_Arena arena;

  String_View suite_filter;
  String_View case_filter;

  List<String_View> failed_suites;

  template <const usize N>
  void run (String_View suite_name, const Test_Case (&cases)[N]) {
    if (!is_empty(suite_filter) && !compare_strings(suite_filter, suite_name)) return;
    print("Suite [%]\n", suite_name);

    for (auto &test_case: cases) {
      if (!is_empty(case_filter) && !compare_strings(case_filter, test_case.name)) return;
      print("Case [%]", test_case.name);

      auto local = arena;

      if (!__builtin_setjmp(context)) {
        if (test_case.before) test_case.before(arena);
        test_case.case_code(arena);
        if (test_case.after) test_case.after(arena);

        print(": SUCCESS\n");
      }
      else {
        print(" -> failed\n");
      }
    }

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

