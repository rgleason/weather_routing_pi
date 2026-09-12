/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 or later.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_EXTERNAL_PLANNING_PROVIDER_H_
#define WEATHER_ROUTING_EXTERNAL_PLANNING_PROVIDER_H_

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>

#include "ocpn_plugin.h"

class weather_routing_pi;

#ifndef OCPN_HAVE_PLANNING_PROVIDER_V1
typedef int (*PlugInPlanningCancelledV1)(void* cancellation_context);
typedef void (*PlugInPlanningProgressV1)(void* progress_context,
                                         double progress);
typedef int (*PlugInPlanningRunV1)(
    void* provider_context, const char* request_json,
    PlugInPlanningCancelledV1 is_cancelled, void* cancellation_context,
    PlugInPlanningProgressV1 report_progress, void* progress_context,
    const char** result_json, const char** error_code,
    const char** error_message);
struct PlugInPlanningProviderV1 {
  std::size_t struct_size;
  const char* capability;
  const char* display_name;
  void* provider_context;
  PlugInPlanningRunV1 run;
};
#endif

/** Optional adapter; absence on a stock host is a supported no-op. */
class ExternalPlanningProvider {
public:
  explicit ExternalPlanningProvider(weather_routing_pi& plugin);
  ~ExternalPlanningProvider();

  bool RegisterIfSupported();
  bool Unregister();
  bool IsRegistered() const { return registered_; }

private:
  static int Run(void* context, const char* request_json,
                 PlugInPlanningCancelledV1 is_cancelled,
                 void* cancellation_context,
                 PlugInPlanningProgressV1 report_progress,
                 void* progress_context, const char** result_json,
                 const char** error_code, const char** error_message);
  int RunRequest(const char* request_json,
                 PlugInPlanningCancelledV1 is_cancelled,
                 void* cancellation_context,
                 PlugInPlanningProgressV1 report_progress,
                 void* progress_context, const char** result_json,
                 const char** error_code, const char** error_message);
  int Fail(const std::string& code, const std::string& message,
           const char** error_code, const char** error_message);

  weather_routing_pi& plugin_;
  std::mutex run_mutex_;
  std::atomic_bool cancellation_requested_{false};
  bool registered_{false};
  std::string result_;
  std::string error_code_;
  std::string error_message_;
};

#endif
