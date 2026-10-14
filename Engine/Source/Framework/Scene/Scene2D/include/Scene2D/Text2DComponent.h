#pragma once

#include "ECS/Component.h"

#include <glm/glm.hpp>

#include <string>

namespace ya
{

/// World-space text on a Node3D, next to a TransformComponent. This is the
/// world-space counterpart of a UI label: a damage number, a unit name, a
/// floating bit of feedback. Not screen-space -- `ScreenDrawList::makeText`
/// stays the GUI path; this is expanded into world sprite candidates, one
/// quad per glyph, by the frame extractor.
///
/// Glyphs come from the engine UI font stack (FontManager). The font's atlas
/// stores coverage in alpha, so a glyph quad tints it exactly like a sprite:
/// `color.rgb` is the ink and `color.a` fades the whole line. There is no
/// per-glyph colour (that is what the GUI richtext path is for).
///
/// Scripts spawn one through `world.spawnText`, which is the only expected
/// author; the fields stay plain so they reflect.
struct YA_SCENE_2D_API Text2DComponent : public IComponent
{
    YA_REFLECT_BEGIN(Text2DComponent, IComponent)
    YA_REFLECT_FIELD(bVisible)
    YA_REFLECT_FIELD(text)
    YA_REFLECT_FIELD(font)
    YA_REFLECT_FIELD(fontPixelSize)
    YA_REFLECT_FIELD(color, .color())
    YA_REFLECT_FIELD(size)        ///< World height of one text line, in world units
    YA_REFLECT_FIELD(pivot)       ///< Normalized anchor; (0.5, 0.5) centres the line on the entity
    YA_REFLECT_FIELD(outlineWidth)  ///< World-unit outline thickness; 0 disables it
    YA_REFLECT_FIELD(outlineColor, .color())
    YA_REFLECT_FIELD(layer)
    YA_REFLECT_FIELD(sortOrder)
    YA_REFLECT_FIELD(bYSort)
    YA_REFLECT_END()

    bool      bVisible = true;
    std::string text;
    /// Font family name (FontManager). Empty uses the runtime default face.
    std::string font;
    /// Device pixel size the face is rasterized at. The scene snapshot is
    /// view-independent, so this cannot follow the camera zoom: pick it near
    /// the on-screen size and let `size` place the line in the world. A pixel
    /// look wants a small size with the glyph sampler's nearest filtering.
    uint32_t  fontPixelSize = 32;
    glm::vec4 color{1.0f};
    /// World height of a text line. The glyph atlas is rasterized at a device
    /// pixel size chosen from this, so a small change stays crisp instead of
    /// resampling one bitmap.
    float     size = 0.5f;
    /// Normalized anchor: (0, 0) is the line's top-left, (1, 1) its
    /// bottom-right, (0.5, 0.5) its centre. The anchor sits on the entity, so
    /// centring on the entity needs no offset in the caller.
    glm::vec2 pivot{0.5f, 0.5f};
    /// Thickness of a coloured border drawn behind the glyphs, in every
    /// direction, measured in font-raster pixels (the same unit as
    /// fontPixelSize -- so the border scales with the text, not with the
    /// scene's tile grid). 0 draws no border. A label over a busy scene needs
    /// one: unoutlined ink at this scale blends into whatever sits behind it,
    /// and the border is what keeps it readable (Godot's Label3D outline_size,
    /// TMP's outline, on the same reasoning). The border quads paint before the
    /// glyphs and do not advance the layout.
    float     outlineWidth = 0.0f;
    glm::vec4 outlineColor{0.0f, 0.0f, 0.0f, 1.0f};
    int32_t   layer     = 0;
    int32_t   sortOrder = 0;
    /// Y-sort against sprites on the same layer, like Sprite2DComponent.
    bool      bYSort = false;
};

} // namespace ya
