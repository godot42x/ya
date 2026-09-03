#include "GUI/Widgets/KeyedVisibleWindow.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ya
{

FKeyedVisibleWindow computeKeyedVisibleWindow(size_t itemCount,
                                             float itemExtent,
                                             float itemSpacing,
                                             float viewportExtent,
                                             float scrollOffset,
                                             size_t overscan)
{
    FKeyedVisibleWindow window;
    itemExtent = std::max(itemExtent, 0.0f);
    itemSpacing = std::max(itemSpacing, 0.0f);
    scrollOffset = std::max(scrollOffset, 0.0f);

    window.contentExtent = itemCount == 0
                               ? 0.0f
                               : static_cast<float>(itemCount) * itemExtent +
                                     static_cast<float>(itemCount - 1) * itemSpacing;
    if (itemCount == 0) {
        return window;
    }

    if (viewportExtent <= 0.0f || itemExtent <= 0.0f) {
        window.count = itemCount;
        return window;
    }

    const float stride = itemExtent + itemSpacing;
    const size_t start = static_cast<size_t>(
        std::min(static_cast<double>(itemCount - 1),
                 std::floor(static_cast<double>(scrollOffset) / static_cast<double>(stride))));
    const float lastVisiblePos = std::max(scrollOffset + viewportExtent - 0.0001f, scrollOffset);
    size_t endExclusive = static_cast<size_t>(
                             std::min(static_cast<double>(itemCount),
                                      std::floor(static_cast<double>(lastVisiblePos) / static_cast<double>(stride)) + 1.0));
    endExclusive = std::max(endExclusive, start + 1);
    endExclusive = std::min(itemCount, endExclusive);

    const size_t first = start > overscan ? start - overscan : 0;
    const size_t last = std::min(itemCount, endExclusive + overscan);
    window.first = first;
    window.count = last - first;
    window.leadingExtent = static_cast<float>(first) * stride;
    const size_t remaining = itemCount - last;
    window.trailingExtent = remaining == 0 ? 0.0f : static_cast<float>(remaining) * stride;
    return window;
}

std::vector<std::string> sliceKeyedVisibleWindow(const std::vector<std::string>& keys,
                                                const FKeyedVisibleWindow& window)
{
    if (window.first >= keys.size() || window.count == 0) {
        return {};
    }
    const size_t count = std::min(window.count, keys.size() - window.first);
    return {keys.begin() + static_cast<std::ptrdiff_t>(window.first),
            keys.begin() + static_cast<std::ptrdiff_t>(window.first + count)};
}

} // namespace ya
