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
  /** True when CORD_VERBOSE=1 is set in the environment. */
  inline bool cord_verbose_enabled()
  {
    static const bool enabled = []() {
      const char *env = std::getenv("CORD_VERBOSE");
      return env != nullptr && env[0] == '1';
    }();
    return enabled;
  }

  /** Trace logging for CoRD hot paths: compile-time IF_DEBUG or runtime CORD_VERBOSE=1. */
  inline bool cord_trace_log(bool if_debug)
  {
    return if_debug || cord_verbose_enabled();
  }

  /** Info-level stdout on CoRD hot paths (honors CORD_VERBOSE / IF_DEBUG via cord_trace_log). */
  inline void cord_trace_out(bool if_debug, const std::string &msg)
  {
    if (cord_trace_log(if_debug))
      std::cout << msg << '\n';
  }
}

#endif