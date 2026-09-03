#pragma once

#include "Core/Api.h"

#include <string>
#include <vector>

namespace ya
{

/// Visible slice of a uniform-stride keyed list. `first` / `count` are item
/// indices; leading/trailing extents are the unmaterialized stacks that keep
/// scroll content height equal to `contentExtent` when only the window is live.
struct FKeyedVisibleWindow
{
    size_t first = 0;
    size_t count = 0;
    float leadingExtent = 0.0f;
    float trailingExtent = 0.0f;
    float contentExtent = 0.0f;

    [[nodiscard]] size_t end() const { return first + count; }
    [[nodiscard]] bool empty() const { return count == 0; }
};

/// Compute the live window for a uniform row list.
///
/// `itemExtent` is the row's authored height; `itemSpacing` is the box gap
/// between rows. A non-positive viewport or row extent materializes the full
/// list so the first layout can still produce content size.
[[nodiscard]] YA_GUI_API FKeyedVisibleWindow computeKeyedVisibleWindow(size_t itemCount,
                                                                      float itemExtent,
                                                                      float itemSpacing,
                                                                      float viewportExtent,
                                                                      float scrollOffset,
                                                                      size_t overscan = 1);

[[nodiscard]] YA_GUI_API std::vector<std::string> sliceKeyedVisibleWindow(const std::vector<std::string>& keys,
                                                                         const FKeyedVisibleWindow& window);

} // namespace ya
