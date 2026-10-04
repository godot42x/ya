#include "Scene2D/TilemapComponent.h"

#include <algorithm>

namespace ya
{

int32_t TilemapComponent::cellAt(int32_t x, int32_t y, size_t layerIndex) const
{
    if (x < 0 || y < 0 || x >= width || y >= height || layerIndex >= layers.size()) {
        return -1;
    }
    const std::vector<int32_t>& cells = layers[layerIndex].cells;
    const int32_t index = cellIndex(x, y);
    if (index < 0 || index >= static_cast<int32_t>(cells.size())) {
        return -1;
    }
    return cells[static_cast<size_t>(index)];
}

void TilemapComponent::onEdit()
{
    // The Inspector writes width/height as plain fields; without this the
    // layers would keep their old cell count and extraction would skip them
    // as mismatched. The previous row width comes from the transient edit
    // dims, so widening 6 -> 10 keeps the 6 left columns exactly.
    if (!isValid()) {
        return;
    }
    const int32_t oldW = _editWidth;
    const int32_t oldH = _editHeight;
    const auto    want = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (TilemapLayer& layer : layers) {
        std::vector<int32_t> next(want, 0);
        if (oldW > 0 && oldH > 0 &&
            layer.cells.size() == static_cast<size_t>(oldW) * static_cast<size_t>(oldH)) {
            const int32_t copyW = std::min(width, oldW);
            const int32_t copyH = std::min(height, oldH);
            for (int32_t y = 0; y < copyH; ++y) {
                for (int32_t x = 0; x < copyW; ++x) {
                    next[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
                        layer.cells[static_cast<size_t>(y) * static_cast<size_t>(oldW) + static_cast<size_t>(x)];
                }
            }
        }
        else {
            // Layer data never matched a known grid (hand-written mismatch):
            // keep the linear prefix instead of inventing a layout.
            const size_t keep = std::min(layer.cells.size(), want);
            for (size_t i = 0; i < keep; ++i) {
                next[i] = layer.cells[i];
            }
        }
        layer.cells.swap(next);
    }
    _editWidth  = width;
    _editHeight = height;
}

void TilemapComponent::onPostSerialize()
{
    _editWidth  = width;
    _editHeight = height;
}

void TilemapComponent::resize(int32_t newWidth, int32_t newHeight)
{
    if (newWidth <= 0 || newHeight <= 0) {
        return;
    }
    if (newWidth == width && newHeight == height && _editWidth == width && _editHeight == height) {
        return;
    }
    width  = newWidth;
    height = newHeight;
    onEdit();
}

bool TilemapComponent::setCell(int32_t x, int32_t y, size_t layerIndex, int32_t value)
{
    if (!isValid() || layerIndex >= layers.size()) {
        return false;
    }
    std::vector<int32_t>& cells = layers[layerIndex].cells;
    if (cells.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        return false;
    }
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    cells[static_cast<size_t>(cellIndex(x, y))] = value;
    return true;
}

int32_t TilemapComponent::fillRect(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY,
                                   size_t layerIndex, int32_t value)
{
    if (!isValid() || layerIndex >= layers.size()) {
        return 0;
    }
    std::vector<int32_t>& cells = layers[layerIndex].cells;
    if (cells.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        return 0;
    }
    const int32_t x0 = std::max(minX, 0);
    const int32_t y0 = std::max(minY, 0);
    const int32_t x1 = std::min(maxX, width - 1);
    const int32_t y1 = std::min(maxY, height - 1);
    int32_t       written = 0;
    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {
            cells[static_cast<size_t>(cellIndex(x, y))] = value;
            ++written;
        }
    }
    return written;
}

void TilemapComponent::serializeCustom(nlohmann::json& out) const
{
    // `layers` is an array, and arrays are compared as a whole, so a default
    // field inside a layer would be written. These two default to "off" and
    // stay out of the file; zOffset is left as the array already stored it.
    const auto layers = out.find("layers");
    if (layers == out.end() || !layers->is_array()) {
        return;
    }
    for (auto& layer : *layers) {
        if (!layer.is_object()) {
            continue;
        }
        if (const auto ySort = layer.find("bYSort");
            ySort != layer.end() && ySort->is_boolean() && !ySort->get<bool>()) {
            layer.erase(ySort);
        }
        if (const auto offset = layer.find("layerOffset");
            offset != layer.end() && offset->is_number_integer() && offset->get<int>() == 0) {
            layer.erase(offset);
        }
    }
}

} // namespace ya
