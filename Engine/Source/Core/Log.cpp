#include "Log.h"


logcc::SyncLogger Logger::CoreLogger;
logcc::SyncLogger Logger::AppLogger;



void Logger::init()
{
    CoreLogger.setFormatter(YaFormatterV2("Core"));
    AppLogger.setFormatter(YaFormatterV2("App"));
}


logcc::SyncLogger &Logger::getLazyLog()
{
    static logcc::SyncLogger *LazyLogger = []() {
        static logcc::SyncLogger logger;
        logger.setFormatter(YaFormatterV2("Lazy"));
        return &logger;
    }();
    return *LazyLogger;
}