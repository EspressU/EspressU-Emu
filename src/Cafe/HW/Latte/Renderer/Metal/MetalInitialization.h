#pragma once

#include <functional>

// Runs a Metal setup step. If it throws a C++ or Objective-C exception, the exception and the last
// reported stage are written to the log before it is passed on.
void RunMetalInitialization(const char* name, const std::function<void()>& initialize);

// Remembers and logs which setup stage is running, so a crash or failure can be located from the log
void LogMetalInitializationStage(const char* stage);
