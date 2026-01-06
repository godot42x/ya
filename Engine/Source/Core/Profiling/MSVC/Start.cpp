#include "Core/Profiling/StaticInitProfiler.h"
#include <cstdlib>
#include <iostream>

#ifdef _MSC_VER

namespace ya::profiling::detail
{
// 导出一个函数来强制链接这个翻译单元
void refMStartSVC()
{
    // 空函数，只是为了让链接器不要优化掉这个文件
    volatile int dummy = 0;
    (void)dummy;
}

struct Start
{
    Start()
    {
        // 强制刷新确保输出
        ya::profiling::StaticInitProfiler::recordStart();
    }
};

    #pragma warning(push)
    #pragma warning(disable : 4073) // warning C4073: initializers put in
    #pragma init_seg(lib)
    #pragma warning(pop)
static Start static_init_start_obj;



} // namespace ya::profiling::detail

#endif
