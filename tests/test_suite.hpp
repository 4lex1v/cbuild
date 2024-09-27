
#include "anyfin/base.hpp"
#include "anyfin/arena.hpp"
#include "anyfin/result.hpp"
#include "anyfin/callsite.hpp"
#include "anyfin/console.hpp"
#include "anyfin/commands.hpp"
#include "anyfin/file_system.hpp"
#include "anyfin/format.hpp"

using namespace Fin;
using namespace Fin;

struct Test_Case {
  typedef void (*Case_Step)(Memory_Arena &arena);

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

struct Test_Errors {
  struct System_Error {
    Fin::System_Error error;
    String context;
    Callsite callsite;
  };

  struct Child_Process_Error {
    u32 status_code;
    String output;
    String context;
    Callsite callsite;
  };

  struct Condition_Error {
    String expression;
    String context;
    Callsite callsite;
  };
};

template <typename T>
static void require_internal (const Sys_Result<T> &result,
                              String expression,
                              String context = "",
                              Callsite callsite = {}) {
  if (result.is_error()) throw Test_Errors::System_Error(result.error.value, context, callsite);

  if constexpr (Same_Types<T, System_Command_Status>) {
    if (result.value.status_code != 0)
      throw Test_Errors::Child_Process_Error(result.value.status_code, result.value.output, context, callsite);
  }
}

static void require_internal (bool result, String expression, String context = "", Callsite callsite = {}) {
  if (!result) throw Test_Errors::Condition_Error(expression, context, callsite);
}

#define require(EXPR) require_internal((EXPR), stringify(EXPR))
#define crequire(EXPR, CALLSITE) require_internal((EXPR), stringify(EXPR), "", (CALLSITE))
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

  String suite_filter;
  String case_filter;

  List<String> failed_suites { arena };

  Status execute_step (Test_Case::Case_Step step) {
    auto local = this->arena;

    try { step(local); }
    catch (const Test_Errors::System_Error &error) {
      auto msg = format_string(local, "\tStatus:\tSYSTEM_ERROR\nPosition:\t[%:%]\nError:\t%\n",
                               error.callsite.file, error.callsite.line, error.error);
      write_to_stdout(msg);
      if (error.context) write_to_stdout(format_string(local, "\tContext:\t%\n", error.context));
      return Status::Failed;
    }
    catch (const Test_Errors::Child_Process_Error &error) {
      auto msg = format_string(local, "\tStatus:\tCHILD_PROCESS_ERROR\n\tPosition:\t[%:%]\n\tReturn Code:\t%\n",
                               error.callsite.file, error.callsite.line, error.status_code);
      if (error.output)  write_to_stdout(format_string(local, "\tOutput:\t%\n", error.output));
      if (error.context) write_to_stdout(format_string(local, "\tContext:\t%\n", error.context));

      return Status::Failed;
    }
    catch (const Test_Errors::Condition_Error &error) {
      auto msg = format_string(local, "\tStatus:\tCONDITION\n\tPosition:\t[%:%]\n\tExpression:\t%,\n",
                               error.callsite.file, error.callsite.line, error.expression);
      write_to_stdout(msg);
      if (error.context) write_to_stdout(format_string(local, "\tContext:\t%\n", error.context));
      return Status::Failed;
    }

    return Status::Success;
  }

  template <const usize N>
  void run (String suite_name, const Test_Case (&cases)[N]) {
    if (is_empty(suite_filter) || suite_filter == suite_name) {
      write_to_stdout(format_string(arena, "Suite: %\n", suite_name));

      for (auto &test_case: cases) {
        if (is_empty(case_filter) || (case_filter == test_case.name)) {
          write_to_stdout(format_string(arena, "  - %\n", test_case.name));

          if (test_case.before && execute_step(test_case.before) == Status::Failed) continue;
          if (execute_step(test_case.case_code) == Status::Failed) list_push_copy(failed_suites, test_case.name);
          if (test_case.after && execute_step(test_case.after) == Status::Failed) return;
        }
      }
    }
  }

  int report () const {
    if (failed_suites.count == 0) {
      write_to_stdout(format_string(arena, "\nSUCCESS"));
      return 0;
    }

    write_to_stdout(format_string(arena, "\n\nFAILED (%): ", failed_suites.count));
    for (auto &name: failed_suites) write_to_stdout(format_string(arena, "%, ", name));
    write_to_stdout(format_string(arena, "\n"));

    return 1;
  }
};
