
#include "Base.h"
#include "log.cc/log.h"

#define NE_CORE_LOG(...)
#define NE_CORE_TRACE(...)
#define NE_CORE_INFO(...)
#define NE_CORE_WARN(...)
#define NE_CORE_ERROR(...)


#define NE_LOG(...)
#define NE_TRACE(...)
#define NE_INFO(...) fprintf(stdout, "%s\n", std::format(__VA_ARGS__).c_str());
#define NE_WARN(...)
#define NE_ERROR(...)


#define NE_CORE_ASSERT(condition, ...)                    \
    {                                                     \
        if (!!!(condition)) {                             \
            NE_CORE_ERROR(ELogLevel::Error, __VA_ARGS__); \
            PLATFORM_BREAK();                             \
        }                                                 \
    }

#define NE_ASSERT(condition, ...)                    \
    {                                                \
        if (!!!(condition)) {                        \
            NE_ERROR(ELogLevel::Error, __VA_ARGS__); \
            PLATFORM_BREAK();                        \
        }                                            \
    }