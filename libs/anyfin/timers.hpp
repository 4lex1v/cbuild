
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/result.hpp"

namespace Fin {

struct Timer_Error {};

static Result<Timer_Error, void> enable_high_precision_timer ();

static void disable_high_precision_timer ();

static u64 get_timer_frequency ();

static u64 get_timer_value ();

static u64 get_elapsed_millis (u64 frequency, u64 from, u64 to);

}

#ifndef FIN_TIMERS_HPP_IMPL
  #ifdef PLATFORM_WIN32
    #include "anyfin/timers_win32.hpp"
  #else
    #error "Unsupported platform"
  #endif
#endif
