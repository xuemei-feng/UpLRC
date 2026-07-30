#ifndef DEVCOMMON
#define DEVCOMMON

#include <iostream>
#include <map>
#include <memory>
#include <string.h>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <algorithm>
#include <unordered_set>
#include <random>
#include <set>
#include <cstdlib>

namespace ECProject
{
  /** True when UPLRC_VERBOSE=1 is set in the environment. */
  inline bool uplrc_verbose_enabled()
  {
    static const bool enabled = []() {
      const char *env = std::getenv("UPLRC_VERBOSE");
      return env != nullptr && env[0] == '1';
    }();
    return enabled;
  }

  /** Trace logging for UpLRC hot paths: compile-time IF_DEBUG or runtime UPLRC_VERBOSE=1. */
  inline bool uplrc_trace_log(bool if_debug)
  {
    return if_debug || uplrc_verbose_enabled();
  }

  /** Info-level stdout on UpLRC hot paths (honors UPLRC_VERBOSE / IF_DEBUG via uplrc_trace_log). */
  inline void uplrc_trace_out(bool if_debug, const std::string &msg)
  {
    if (uplrc_trace_log(if_debug))
      std::cout << msg << '\n';
  }
}

#endif