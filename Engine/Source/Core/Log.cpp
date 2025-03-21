#include "Log.h"



logcc::SyncLogger Logger::CoreLogger;
logcc::SyncLogger Logger::AppLogger;

void Logger::init()
{
    CoreLogger.setFormatter(logcc::CategoryFormatter("Core"));
    AppLogger.setFormatter(logcc::CategoryFormatter("App"));
}
