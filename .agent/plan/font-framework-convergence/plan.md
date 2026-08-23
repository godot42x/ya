# 字体框架收敛 Plan：Rasterizer 抽象 + 动态图集（首期 Bitmap 后端）

> 建立日期：2026-08-22
> 状态：活跃计划
> 目标：把字体子系统从「FreeType 固定图集 + 每缺失字符一张 standalone 纹理」收敛为
>   「IFontRasterizer 抽象 + 动态可扩容图集」，一步到位收口架构，后续 MSDF/SDF 只
>   新增后端、不动上层。

## 0. 现状（已核对代码，2026-08-22）

- 图集：48px 单基 atlas（RGBA8，白色+alpha coverage），16/行 shelf 布局，1px padding，
  pow2 尺寸；`FontAtlas_*` 经 `resolveSamplerForTexture` 用 `linear_clamp` 采样。
- 动态字符：`ensureGlyphs` 对缺失码位走 `appendStandaloneGlyph` —— 每字符一张独立
  `Texture`（RGBA8）→ CJK 场景 GPU 资源爆炸、draw 次数随字符数增长。
- 缩放：`getFont(name, size≠48)` 返回 `makeScaledView`（共享基 atlas，度量等比缩放）
  —— 内存平坦（98aac34b 的既定决策，**不可回退**），但非基尺寸文字模糊（MSDF 阶段解决）。
- **既有 Core Rule 6 违规**：`Render2D::drawText` 在命令录制中调用 `ensureGlyphs`，
  中途经 `Texture::fromData` 创建 GPU 纹理。本计划一并修正。
- 贴图槽预算：sprite 管线 `TEXTURE_SET_SIZE 16`（每顶点 textureIdx，Sprite2D.slang
  define）；每 font 图集恒占 1 槽 → **图集必须保持单纹理**（扩容 = 重建更大纹理 +
  repack，不做多页）。
- R8 前置事实：`EFormat::R8_UNORM` 枚举存在（RenderDefines.h:395，Vulkan/GL 原生）；
  但 `Sprite2D.slang::sampleTexturedSprite` 是「RGBA 采样 × tint、以 `.a` 判 discard」，
  直接换 R8 图集会因 swizzle `(r,0,0,1)` 渲染成黑字 → 首期保持 RGBA8。

## 1. 架构（方案 A 抽象 + 首期 Bitmap 实现）

```
┌─────────────────────────────────────────────────┐
│                  Font (资产)                     │
│  fontPath / metrics / renderMode / atlas        │
│  characters: codepoint → Character(Glyph)       │
└──────────────────┬──────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────┐
│          IFontRasterizer (抽象接口)              │
│  rasterize(face, codepoint, pixelSize)          │
│  getAtlasFormat() → EFormat                     │
│  getShaderKind() → FontShaderKind               │
└──────┬──────────────┬──────────────┬────────────┘
       │              │              │
┌──────▼─────┐ ┌──────▼─────┐ ┌──────▼─────┐
│ BitmapRast │ │  MSDFRast  │ │  SDFRast   │
│ (首期实现)  │ │  (后期)    │ │  (后期)    │
└────────────┘ └────────────┘ └────────────┘
       │
┌──────▼──────────────────────────────────────────┐
│            DynamicFontAtlas (动态图集)            │
│  可扩容 shelf-packing，替代固定图集+standalone    │
└─────────────────────────────────────────────────┘
```

**关键设计原则**：抽象层一次到位，首期只填 Bitmap 实现。后期加 MSDF 只需新增一个
rasterizer 子类 + shader 分支，不动上层。

## 2. Phase 1 改动清单

### Module 1：核心数据结构重构

文件：`Engine/Source/Framework/Render/Resources/FontManager.h`

- 新增渲染模式枚举：

```cpp
enum class EFontRenderMode : uint8_t
{
    Bitmap = 0,  // 首期唯一实现
    MSDF,        // 预留
    SDF,         // 预留
};
```

- `Character` 统一为单一图集内字形（替代现有「图集内 / standalone」双职责）：

```cpp
struct Character
{
    glm::vec4  uvRect;     // (offsetU, offsetV, scaleU, scaleV)
    glm::ivec2 size;       // 像素尺寸
    glm::ivec2 bearing;    // baseline 偏移
    glm::vec2  advance;    // 步进
    bool       bInAtlas = true;  // 恒 true；standaloneTexture 字段删除
};
```

- `Font` 增加 `renderMode`；`atlasTexture` 由 DynamicFontAtlas 管理，Font 持引用。

### Module 2：动态图集 DynamicFontAtlas

新文件：`Engine/Source/Framework/Render/Resources/DynamicFontAtlas.h/.cpp`

- shelf-packing 行式布局（现有逻辑升级），运行时追加字形；
- **单纹理约束**（槽预算）：满 → 新建 2x 更大纹理 + 重 pack 已有 + 新字形，
  `onRepack(codepoint → 新 uvRect)` 回调更新；**不做多页**；
- **首期格式 RGBA8**（零 shader 改动；R8 见 Module 5 可选子任务）；
- 上传走 `Texture::fromData` / `TextureUploadService`（整纹理 staging+copy），
  **不在帧录制中途执行**（见 Module 4 pending/flush）。

### Module 3：光栅器抽象 + Bitmap 实现

新文件：
- `Engine/Source/Framework/Render/Resources/IFontRasterizer.h`
- `Engine/Source/Framework/Render/Resources/BitmapFontRasterizer.h/.cpp`

```cpp
struct GlyphBitmap
{
    uint32_t width = 0, height = 0;
    glm::ivec2 bearing;
    glm::vec2  advance;
    std::vector<uint8_t> pixels;  // 格式由 getAtlasFormat() 决定
};

class IFontRasterizer
{
public:
    virtual ~IFontRasterizer() = default;
    virtual EFontRenderMode getMode() const = 0;
    virtual EFormat getAtlasFormat() const = 0;
    virtual GlyphBitmap rasterize(FT_Face face, uint32_t codepoint, uint32_t pixelSize) = 0;
};
```

`BitmapFontRasterizer`：迁移 `makeGlyphCharacter` / `appendStandaloneGlyph` 的
`FT_Load_Char + FT_LOAD_RENDER` 逻辑，输出灰度位图（R8 数据，图集侧按 RGBA8 填
白+alpha，为后续 R8 图集留出纯灰度源）。

### Module 4：FontManager 重构

文件：`Engine/Source/Framework/Render/Resources/FontManager.cpp`

1. `loadFont`：创建 DynamicFontAtlas + BitmapFontRasterizer，预光栅 ASCII 95 进图集。
2. `ensureGlyphs` → **只登记 pending 请求，不建纹理**；
   新增 `FontManager::flushPendingGlyphs(IRender&)`：按码位光栅化 → `atlas.addGlyph()`
   → 满则扩容 repack → 更新 Character.uvRect；
3. **安全时机**：`GUIAppHost` 帧循环在 `buildSnapshot` 之后、`cmdBuf->begin()` 之前
   调 `flushPendingGlyphs`（安全点已核对：GUIAppHost.cpp 帧函数内 dump 钩子同处）；
   `Render2D::drawText` 中的 `ensureGlyphs` 改为纯查询（缺失码位渲染 '?'，下一帧补齐
   —— 1 帧延迟为业界常规）。
4. scaled view 机制**保留**（flat memory 决策不回退）；repack 后 `refreshScaledView`
   同步全部视图 uvRect。
5. repack 产生新纹理后重触发 `FontAtlasTextureSink`（AssetManager 注册新纹理）。

### Module 5：Shader

- **首期零改动**（图集保持 RGBA8，现有 `sampleTexturedSprite` 行为等价）。
- 【可选子任务，单独验收】R8 图集（省 75% 显存）：前提是新增文本管线变体
  （coverage 采样 `float coverage = tex.r; frag = vec4(tint.rgb, tint.a*coverage)`），
  且 Vulkan/GL 双后端验证；不通过则回退 RGBA8 只填 R 通道（功能正常、不省显存）。
- MSDF/SDF 分支在 shader 留 TODO 注释（Phase 2/3）。

### Module 6：Render2D 集成

文件：`Engine/Source/Framework/Render/Render2D/QuadRender.cpp`

1. `drawText` 移除 `character.bInAtlas` 分支，全部走 `drawSubTexture` + atlasTexture；
2. `resolveSamplerForTexture` 的 FontAtlas 判断保留（ClampLinear）；
3. `drawText` 内不再调用 `ensureGlyphs`（见 Module 4）。

### Module 7：GUI Compose 层

`Render2DComposePass` / `UIFrameSnapshot` 零改动（只持 `Font*` 与 text，经
`Render2D::makeText`，底层变化透明）。

## 3. Phase 1 交付物与验收标准

### 交付物

| 模块 | 文件 | 状态 |
|---|---|---|
| 数据结构 | `FontManager.h` 重构（EFontRenderMode / Character / Font） | 改动 |
| 动态图集 | `DynamicFontAtlas.h/.cpp` | 新增 |
| 光栅器抽象 | `IFontRasterizer.h` | 新增 |
| 位图光栅器 | `BitmapFontRasterizer.h/.cpp` | 新增 |
| FontManager | `FontManager.cpp` 重构 + pending/flush | 改动 |
| Host 安全点 | `GUIAppHost.cpp` flush 调用点 | 改动 |
| Shader | 首期零改动（R8 可选子任务独立验收） | — |
| QuadRender | `drawText` 简化 | 改动 |

### 验收标准

1. **ASCII 文字**：渲染质量不低于现有水平（RGBA8 图集路径行为等价）。
2. **CJK 文字**：中文/日文/韩文正常渲染，所有字形进同一动态图集，无每字符独立纹理。
3. **图集扩容**：大量生僻字触发扩容后，已有文字 UV 正确更新、无渲染错误；
   `FontAtlasTextureSink` 对新纹理重触发。
4. **显存**：首期 RGBA8 与现持平；R8 为可选子任务，独立验收，失败回退不阻塞主线。
5. **缩放**：scaled view 机制保留，非整数倍缩放行为与现有一致（放大模糊是预期，
   MSDF 阶段解决）。
6. **编译**：Vulkan + OpenGL 双后端编译通过；MSVC 门禁由 cross-platform skill 走查。
7. **扩展点**：`IFontRasterizer` 可直接新增 MSDF 实现，不改 FontManager/QuadRender 上层。
8. **Core Rule 6 修正验收**：帧录制期不再创建字体纹理（pending/flush 是唯一路径，
   测试/日志断言）。
9. **回归**：全量 GUI 场景（21）+ 闭包测试（137）+ 样式基线门通过（基线按需重生成）。

## 4. 后期 Phase（预留，不实现）

### Phase 2：MSDF 后端
- 新增 `MSDFFontRasterizer`，集成 `msdfgen`（core + freetype bridge，MIT）；
- 图集格式 RGB8，shader 启用 median-of-3 MSDF 分支（`dist=(median(rgb)-0.5)*pxRange`）；
- `Font.renderMode = MSDF`；适用 game UI / 世界文字 / 可缩放文本；
- 接缝约定：`generateDistanceGlyph(face, cp, flavor)` 与 shader 采样函数是唯二改动面。

### Phase 3：SDF 后端 + 特效（描边/发光）

### Phase 4：自动模式选择
- `TextComponent.usage`（EditorBody / GameUI / WorldText / Display）→ FontManager
  自动选 renderMode，达成三后端混合。

## 5. 工作量估算

| 模块 | 预估工时 | 说明 |
|---|---|---|
| 数据结构重构 + DynamicFontAtlas | 2 天 | 核心，shelf packing + 扩容 repack |
| IFontRasterizer + BitmapFontRasterizer | 1 天 | 从现有代码迁移 |
| FontManager 重构 + pending/flush | 1.5 天 | loadFont/ensureGlyphs 重写 + 安全时机 |
| Shader（首期零改动；R8 可选 1 天） | 0（可选 +1） | 独立验收 |
| QuadRender 集成 + 测试 | 1 天 | drawText 简化 + CJK/扩容验证 |
| **合计** | **约 5.5–6.5 天** | |

## 6. 风险与注意事项

1. **R8 纹理格式**：枚举存在、双后端原生支持，但**现 shader 结构下直接换会黑字**
   （Sprite2D.slang 已核对）→ 必须走独立文本管线变体子任务，不通过即回退。
2. **图集扩容时机**：repack = 重建 GPU 纹理，必须发生在帧边界安全点（buildSnapshot
   之后、录制之前），经 pending/flush 机制保证（Core Rule 6）。
3. **scaled view 与动态图集的交互**：base 图集 repack 后，所有 scaled view 的 uvRect
   经 `refreshScaledView` 同步。
4. **ImGui 字体不受影响**：编辑器 ImGui 走自己的图集（含 CJK/emoji merged stack），
   本重构只影响自有 retain-mode GUI 框架；后续（Phase 后置项）把 ImGui 的字体栈
   迁移到共享 FontManager 链时单独评估。
5. **Repack 的 sink 重触发**：`FontAtlasTextureSink` 消费者（AssetManager）持旧纹理
   引用，repack 后必须收到新纹理。
6. **测量语义**：光栅化仍以 48px 基 atlas + scaled view 进行，真实度量不变（本阶段
   不引入 per-size atlas，不回退 flat memory 决策）。

## 7. 当前默认决策

1. 首期只实现 Bitmap 后端；2. 抽象层（IFontRasterizer + 动态图集）一次到位；
3. 图集单纹理 + repack（槽预算）；4. 首期 RGBA8（R8 为可选子任务）；
5. glyph 捕获移出录制期（pending/flush 修 Rule 6 违规）；6. scaled view 保留；
7. MSDF 为既定升级路（接缝已命名），不做单通道 SDF。
