
#pragma once

#include "Core/Base.h"
#include <glm/glm.hpp>



namespace ya
{


using layer_idx_t = uint32_t;

struct UIElement;

struct UIRenderContext
{
};

struct FUIColor
{
    glm::vec4 data = glm::vec4(1.0f);

    FUIColor() = default;

    FUIColor(float r, float g, float b, float a)
        : data(r, g, b, a)
    {
    }

    [[nodiscard]] const glm::vec4 &asVec4() const { return data; }
};


struct FUIHelper
{
    static bool isPointInRect(const glm::vec2 &point, const glm::vec2 &rectPos, const glm::vec2 &rectSize)
    {
        return point.x >= rectPos.x &&
               point.x <= rectPos.x + rectSize.x &&
               point.y >= rectPos.y &&
               point.y <= rectPos.y + rectSize.y;
    }


    static bool isUIPendingKill(UIElement *el)
    {
        return el == nullptr; // kill flag....
    }
    static bool isValid(UIElement *el)
    {
        return el != nullptr && !isUIPendingKill(el);
    }
};


struct UILayout
{
    float borderWidth = 1.0f;
};


struct UIAppCtx
{
    glm::vec2 lastMousePos;
};


// Spatial grid for optimizing hit testing
struct UISpatialGrid
{
    struct Cell
    {
        std::vector<UIElement *> elements;
    };

    int               _gridWidth  = 10;
    int               _gridHeight = 10;
    float             _cellWidth  = 100.0f;
    float             _cellHeight = 100.0f;
    std::vector<Cell> _cells;

    UISpatialGrid(int gridWidth = 10, int gridHeight = 10, float viewportWidth = 1000.0f, float viewportHeight = 1000.0f)
        : _gridWidth(gridWidth), _gridHeight(gridHeight)
    {
        _cellWidth  = viewportWidth / static_cast<float>(gridWidth);
        _cellHeight = viewportHeight / static_cast<float>(gridHeight);
        _cells.resize(static_cast<size_t>(gridWidth) * static_cast<size_t>(gridHeight));
    }

    void clear()
    {
        for (auto &cell : _cells)
        {
            cell.elements.clear();
        }
    }

    void insert(UIElement *element, const glm::vec2 &position, const glm::vec2 &size)
    {
        int minX = std::max(0, (int)(position.x / _cellWidth));
        int maxX = std::min(_gridWidth - 1, (int)((position.x + size.x) / _cellWidth));
        int minY = std::max(0, (int)(position.y / _cellHeight));
        int maxY = std::min(_gridHeight - 1, (int)((position.y + size.y) / _cellHeight));

        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                int index = y * _gridWidth + x;
                _cells[index].elements.push_back(element);
            }
        }
    }

    std::vector<UIElement *> query(const glm::vec2 &point)
    {
        int x = (int)(point.x / _cellWidth);
        int y = (int)(point.y / _cellHeight);

        if (x < 0 || x >= _gridWidth || y < 0 || y >= _gridHeight)
        {
            return {};
        }

        int index = y * _gridWidth + x;
        return _cells[index].elements;
    }
};


}; // namespace ya