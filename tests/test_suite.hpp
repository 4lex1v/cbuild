
#include "anyfin/base.hpp"

#include "anyfin/core/arena.hpp"
#include "anyfin/core/result.hpp"
#include "anyfin/core/callsite.hpp"

#define WIN32_DEBUG_OUTPUT_ENABLE
#include "anyfin/platform/console.hpp"
#include "anyfin/platform/commands.hpp"
#include "anyfin/platform/file_system.hpp"

using namespace Fin::Core;
using namespace Fin::Platform;

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

struct Test_Errors {
  struct System_Error {
    Fin::Platform::System_Error error;
    String_View context;
    Callsite_Info callsite;
  };

  struct Child_Process_Error {
    u32 status_code;
    String_View output;
    String_View context;
    Callsite_Info callsite;
  };

  struct Condition_Error {
    String_View expression;
    String_View context;
    Callsite_Info callsite;
  };
};

template <typename T>
static void require_internal (const Fin::Platform::Result<T> &result,
                              String_View expression,
                              String_View context = "",
                              Callsite_Info callsite = {}) {
  if (!result) throw Test_Errors::System_Error(result.status, context, callsite);

  if constexpr (Same_Types<T, System_Command_Status>) {
    if (result.value.status_code != 0)
      throw Test_Errors::Child_Process_Error(result.value.status_code, result.value.output, context, callsite);
  }
}

static void require_internal (bool result, String_View expression, String_View context = "", Callsite_Info callsite = {}) {
  if (!result) throw Test_Errors::Condition_Error(expression, context, callsite);
}

#define require(EXPR) require_internal((EXPR), stringify(EXPR))
#define frequire(EXPR, CONTEXT) require_internal((EXPR), stringify(EXPR), (CONTEXT))

#define require_crash(EXPR)                     \
  do {                                          \
    bool exception_captured = false;            \
    try { (EXPR); }                             \
    catch (...) {                               \
      exception_captured = true;                \
    }                                           \
    require(exception_captured);                \
  } while(0)

struct Test_Suite_Runner {
  enum struct Status { Success, Failed };

  mutable Memory_Arena arena;

  String_View suite_filter;
  String_View case_filter;

  List<String_View> failed_suites { arena };

  Status execute_step (const Test_Case::Case_Step step) {
    auto local = this->arena;

    try { step(local); }
    catch (const Test_Errors::System_Error &error) {
      print("   Status:\tSYSTEM_ERROR\n"
            "   Position:\t[%:%]\n"
            "   Error:\t%\n",
            error.callsite.file, error.callsite.line, error.error);
      if (error.context) print("\tContext:\t%\n", error.context);
      return Status::Failed;
    }
    catch (const Test_Errors::Child_Process_Error &error) {
      print("   Status:\tCHILD_PROCESS_ERROR\n"
            "   Position:\t[%:%]\n"
            "   Return Code:\t%\n",
            error.callsite.file, error.callsite.line, error.status_code);
      if (error.output) print("   Output:\t%\n", error.output);
      if (error.context) print("\tContext:\t%\n", error.context);
      return Status::Failed;
    }
    catch (const Test_Errors::Condition_Error &error) {
      print("\tStatus:\tCONDITION\n"
            "\tPosition:\t[%:%]\n"
            "\tExpression:\t%,\n",
            error.callsite.file, error.callsite.line, error.expression);
      if (error.context) print("\tContext:\t%\n", error.context);
      return Status::Failed;
    }

    return Status::Success;
  }

  template <const usize N>
  void run (const String_View &suite_name, const Test_Case (&cases)[N]) {
    if (is_empty(suite_filter) || compare_strings(suite_filter, suite_name)) {
      print("Suite: %\n", suite_name);

      for (auto &test_case: cases) {
        if (is_empty(case_filter) || compare_strings(case_filter, test_case.name)) {
          print("  - %\n", test_case.name);

          if (test_case.before && execute_step(test_case.before) == Status::Failed) continue;
          if (execute_step(test_case.case_code) == Status::Failed) list_push_copy(failed_suites, test_case.name);
          if (test_case.after && execute_step(test_case.after) == Status::Failed) return;
        }
      }
    }
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
