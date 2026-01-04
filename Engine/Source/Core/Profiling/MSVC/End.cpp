#include "Core/Profiling/StaticInitProfiler.h"

#ifdef _MSC_VER

namespace ya::profiling::detail
{


void refEndMSVC()
{
    // 空函数，只是为了让链接器不要优化掉这个文件
    volatile int dummy = 0;
    (void)dummy;
}

// struct End
// {
//     End()
//     {
//         std::atexit([]() {
//             StaticInitProfiler::recordEnd();
//         });
//     }
// };
//     #pragma init_seg(lib)
// End static_init_end_obj;
// static int static_init_end_obj = (atexit([]() {
//                                       ya::profiling::StaticInitProfiler::recordEnd();
//                                   }),
//                                   0);


} // namespace ya::profiling::detail
#endif