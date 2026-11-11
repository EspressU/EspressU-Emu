#include "Cafe/HW/Latte/Renderer/Metal/MetalInitialization.h"
#include "Cemu/Logging/CemuLogging.h"

#import <Foundation/Foundation.h>
#include <exception>

static const char* s_currentStage = "start";

void LogMetalInitializationStage(const char* stage)
{
    s_currentStage = stage;
    cemuLog_log(LogType::Force, "Metal initialization: {}", stage);
    cemuLog_waitForFlush();
}

void RunMetalInitialization(const char* name, const std::function<void()>& initialize)
{
    s_currentStage = name;
    @try
    {
        try
        {
            initialize();
        }
        catch (const std::exception& error)
        {
            cemuLog_log(LogType::Force, "Metal initialization failed during {} ({}): {}", s_currentStage, name, error.what());
            cemuLog_waitForFlush();
            throw;
        }
    }
    @catch (NSException* exception)
    {
        cemuLog_log(LogType::Force, "Metal initialization failed during {} ({}): {}: {}", s_currentStage, name,
                    exception.name.UTF8String ?: "unknown exception",
                    exception.reason.UTF8String ?: "no reason provided");
        cemuLog_log(LogType::Force, "Metal exception call stack: {}",
                    exception.callStackSymbols.description.UTF8String ?: "unavailable");
        cemuLog_waitForFlush();
        @throw;
    }
}
