
#pragma once

#include "base.hpp"
#include "core.hpp"
#include "strings.hpp"

struct Status_Code {
  enum struct Value: int {
    Success,
    System_Error,
    System_Command_Error,
    Resource_Missing,
    Resource_Already_Exists,
    Invalid_Value,
    Load_Error,
    Build_Error,
    Out_Of_Memory,
    User_Command_Error,
    COUNT
  };

  using enum Value;

  Value       value;
  const char *details;
  u64         code;

  constexpr Status_Code (Value _value): value { _value }, details { nullptr }, code { 0 } {}
  constexpr Status_Code (Value _value, u64 _code): value { _value }, details { nullptr }, code { _code } {}
  constexpr Status_Code (Value _value, const char *_details): value { _value }, details { _details }, code { 0 } {}
  constexpr Status_Code (Value _value, const char *_details, u64 _code): value { _value }, details { _details }, code { _code } {}

  bool operator == (Value _value) const { return value == _value; }

  operator bool () const { return value == Success; }
};

static String to_string (Memory_Arena *arena, const Status_Code &status) {
  const char * table[] = {
    "Success",
    "System_Error",
    "System_Command_Error",
    "Resource_Missing",
    "Resource_Already_Exists",
    "Invalid_Value",
    "Load_Error",
    "Build_Error",
    "Out_Of_Memory",
    "User_Command_Error",
  };

  static_assert(array_count_elements(table) == static_cast<usize>(Status_Code::COUNT));

  auto status_string = table[static_cast<usize>(status.value)];

  auto details = status.details ? status.details : "Not-Available";
  return format_string(arena, "\nStatus: %\nSystem Error Code: %\nDetails: %", status_string, status.code, details);
}

template <typename T, typename = void> struct Has_Not_Operator: std::false_type {};
template <typename T> struct Has_Not_Operator<T, std::void_t<decltype(!std::declval<T>())>>: std::true_type {};
template <typename T> inline constexpr bool has_not_operator_v = Has_Not_Operator<T>::value;

template <typename T>
struct Result {
  Status_Code status;
  T           value;

  Result (Status_Code::Value _status): status { _status }, value {} {}
  Result (Status_Code::Value _status, const char *_details): status { _status, _details }, value {} {}

  Result (Status_Code _status): status { _status }, value {} {}

  Result (const T & _value): status { Status_Code::Success }, value { _value } {}
  Result (T &&_value): status { Status_Code::Success }, value { move(_value) } {}

  cb_forceinline operator T& () { return value; }

  cb_forceinline T * operator & () { return &value; }
  cb_forceinline T * operator -> () { return &value; }

  cb_forceinline T operator * () { return value; }

  bool operator == (Status_Code::Value code) {
    return status.value == code;
  }
};

template <typename T> struct Is_Result:            std::false_type {};
template <typename T> struct Is_Result<Result<T>>: std::true_type {};
template <typename T> inline constexpr bool is_result_v = Is_Result<T>::value;

template <typename T> constexpr Status_Code capture_status (const T &result) {
  if constexpr (std::is_same_v<T, Status_Code>) return result;
  else {
    static_assert(is_result_v<T>);
    return result.status;
  }
} 

#define check_status(STATUS)                          \
  do {                                                \
    auto &&expr_result = (STATUS);                    \
    auto captured      = capture_status(expr_result); \
    if (!captured) return captured;                   \
  } while (0)

#define ensure_success(STATUS, MESSAGE)                     \
  do {                                                      \
    auto        expr_result = (STATUS);                     \
    Status_Code __status    = capture_status(expr_result);  \
    String      __message   = (MESSAGE);                    \
    if (!__status) raise_error_and_halt(__message);         \
  } while (0) 

template <String_Convertible T>
constexpr String to_string (Memory_Arena *arena, Result<T> &result) {
  if (result.status == Status_Code::Success) {
    if constexpr (std::convertible_to<T, String>)
      return static_cast<String>(result.value);
    else {
      static_assert(Has_To_String_Defined<T>);
      return to_string(arena, result.value);
    }
  }
  else {
    return to_string(arena, result.status);
  }
}

