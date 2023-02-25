#include <string>

constexpr int WIN_WIDTH  = 800;
constexpr int WIN_HEIGHT = 600;
// struct logger_config
// {
//     std::string               name      = "run_logger";
//     spdlog::level::level_enum level     = spdlog::level::trace;
//     std::string               format    = "[%Y-%m-%d %H:%M:%S.%f] [%P] [%^%l%$] [%s:%# %!()] %v";
//     std::string               file_name = "../log/run.log";
//     // by_size按大小分割 by_day按天分割 by_hour按小时分割
//     std::string  roll_type     = "by_day";
//     unsigned int reserve_count = 10;
//     unsigned int roll_size     = 1024 * 1024 * 100; // 默认100 MB
//     // 表示按天切割的时刻，具体是时刻通过rotation_hour:rotation_minute指定
//     unsigned int rotation_hour   = 0;
//     unsigned int rotation_minute = 0;
//     // 异步日志线程池大小
//     unsigned int async_thread_pool_size = 1;
// };
