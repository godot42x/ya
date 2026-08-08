
#pragma once
#include "Core/Common/FWD-std.h"
#include "utility.cc/stack_deleter.h"

namespace ya
{

using Deleter = ut::StackDeleter;


inline uint32_t nextAligned(uint32_t value, uint32_t alignment)
{
    // 1. 校验：对齐值必须是正整数且是2的幂（核心前提，避免错误计算）
    // 2的幂的二进制特征：只有1位是1，其余是0，因此 alignment & (alignment - 1) == 0
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
    {
        // 非法对齐值，直接返回原值（也可根据需求改为断言/报错）
        return value;
    }

    // 2. 计算对齐掩码：~(alignment - 1)，用于清零低位
    // 例如alignment=8时，alignment-1=7(0b00000111)，掩码=~7=0xFFFFFFF8(高位全1，最后3位0)
    uint32_t alignment_mask = ~(alignment - 1);

    // 3. 向上进位：让数值跨过当前对齐区间，确保后续掩码操作能得到下一个对齐值
    // 例如value=10, alignment=8时，10+7=17，刚好跨过16（当前对齐值上限）
    uint32_t value_with_carry = value + alignment - 1;

    // 4. 掩码清零：保留高位对齐部分，得到最终对齐值
    uint32_t aligned_value = value_with_carry & alignment_mask;

    return aligned_value;
}


} // namespace ya