#include "Log.h"


logcc::SyncLogger Logger::CoreLogger;
logcc::SyncLogger Logger::AppLogger;



void Logger::init()
{
    CoreLogger.setFormatter(YaFormatterV2("Core"));
    AppLogger.setFormatter(YaFormatterV2("App"));
}
