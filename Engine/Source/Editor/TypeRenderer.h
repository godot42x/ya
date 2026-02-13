#pragma once

#include "reflects-core/lib.h"

#include "Core/Reflection/ContainerProperty.h"
#include "Core/Reflection/MetadataSupport.h"
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>


#include "Editor/ReflectionCache.h"

#include <imgui.h>
namespace ya
{

// ============================================================================
// MARK: Constants
// ============================================================================
static constexpr int   MAX_RECURSION_DEPTH  = 10;
static constexpr float CHILD_CLASS_INDENT  = 8.0f;
// static constexpr bool EXPERMENTIAL_ASYNC_PROPERTY_CHANGED = false;

// ============================================================================
// MARK: Deferred Modification Queue (Global Singleton)
// ============================================================================

/**
 * @brief 延迟修改记录 - 用于异步回调（如文件选择器）记录修改
 *
 * 设计原理：
 * - 异步操作（FilePicker等）无法直接与同步的 RenderContext 配合
 * - 异步回调将修改记录到全局队列
 * - 下一帧渲染时 RenderContext::beginInstance() 自动收集匹配的延迟修改
 */
// struct DeferredModification
// {
//     const void *instancePtr = nullptr; ///< Instance that was modified
//     std::string propPath;              ///< Property path within instance
//     uint64_t    timestamp = 0;         ///< Frame timestamp for cleanup
// };

/**
 * @brief 全局延迟修改队列 - 跨帧传递异步修改信息
 *
 * 使用场景：
 * ```cpp
 * // 在异步回调中（如 FilePicker）
 * DeferredModificationQueue::get().enqueue(instancePtr, "textureRef._path");
 *
 * // 在下一帧渲染时，RenderContext 自动收集
 * ctx.beginInstance(materialPtr);  // 内部调用 collectDeferredModifications()
 * renderReflectedType(...);
 * if (ctx.isModifiedPrefix("textureRef")) {  // 包括延迟修改
 *     material->syncTextures();
 * }
 * ```
 */
// class DeferredModificationQueue
// {
//   public:
//     static DeferredModificationQueue &get()
//     {
//         static DeferredModificationQueue instance;
//         return instance;
//     }

//     /**
//      * @brief 添加延迟修改记录（从异步回调调用）
//      * @param instancePtr 被修改的实例指针
//      * @param propPath 属性路径（相对于实例）
//      */
//     void enqueue(const void *instancePtr, const std::string &propPath)
//     {
//         // std::lock_guard<std::mutex> lock(_mutex);
//         _modifications.push_back({.instancePtr = instancePtr,
//                                   .propPath    = propPath,
//                                   .timestamp   = _currentFrame});
//     }

//     /**
//      * @brief 收集并移除指定实例的所有延迟修改
//      * @param instancePtr 目标实例指针
//      * @return 该实例的所有待处理修改
//      */
//     std::vector<DeferredModification> collectForInstance(const void *instancePtr)
//     {
//         // std::lock_guard<std::mutex> lock(_mutex);
//         std::vector<DeferredModification> result;
//         if (_instanceModificationCount.find(instancePtr) == _instanceModificationCount.end()) {
//             return result;
//         }

//         auto it = _modifications.begin();
//         while (it != _modifications.end()) {
//             if (it->instancePtr == instancePtr) {
//                 result.push_back(*it);
//                 it = _modifications.erase(it);
//                 _instanceModificationCount[instancePtr]--;
//             }
//             else {
//                 ++it;
//             }
//         }
//         return result;
//     }

//     /**
//      * @brief 每帧开始时调用，更新时间戳并清理过期记录
//      * @param maxAge 最大保留帧数（默认2帧）
//      */
//     void onFrameBegin(uint64_t maxAge = 2)
//     {
//         // std::lock_guard<std::mutex> lock(_mutex);
//         ++_currentFrame;

//         // Remove stale modifications (older than maxAge frames)
//         auto it = _modifications.begin();
//         while (it != _modifications.end()) {
//             if (_currentFrame - it->timestamp > maxAge) {
//                 it = _modifications.erase(it);
//                 _instanceModificationCount[it->instancePtr]--;
//             }
//             else {
//                 ++it;
//             }
//         }
//     }

//     uint64_t getCurrentFrame() const { return _currentFrame; }

//   private:
//     DeferredModificationQueue() = default;

//     std::vector<DeferredModification>          _modifications;
//     std::unordered_map<const void *, uint32_t> _instanceModificationCount;
//     // std::mutex _mutex;
//     uint64_t _currentFrame = 0;
// };

// ============================================================================
// MARK: Property Identifier (for efficient modification tracking)
// ============================================================================

/**
 * @brief PropertyId - 用于高效标识属性的唯一ID
 *
 * 设计原理：
 * - 使用 uint64_t 哈希值作为属性标识
 * - 由 (实例地址 XOR 属性路径哈希) 组成，确保不同对象的同名属性有不同ID
 * - 支持 O(1) 查询某属性是否被修改
 *
 * 使用场景：
 * ```cpp
 * if (ctx.isModified(PropertyId::make(material, "params"))) {
 *     material->setParamsDirty();
 * }
 * ```
 */
struct PropertyId
{
    uint64_t id = 0;

    PropertyId() = default;
    explicit PropertyId(uint64_t val) : id(val) {}

    // 从实例指针 + 属性路径生成唯一ID
    static PropertyId make(const void *instance, const std::string &propPath)
    {
        // FNV-1a hash for property path
        uint64_t pathHash = 14695981039346656037ULL;
        for (char c : propPath) {
            pathHash ^= static_cast<uint64_t>(c);
            pathHash *= 1099511628211ULL;
        }
        // XOR with instance address for uniqueness across objects
        uint64_t instanceHash = reinterpret_cast<uint64_t>(instance);
        return PropertyId{instanceHash ^ pathHash};
    }

    // 仅从属性路径生成ID（用于同类型属性的通用匹配）
    static PropertyId fromPath(const std::string &propPath)
    {
        uint64_t pathHash = 14695981039346656037ULL;
        for (char c : propPath) {
            pathHash ^= static_cast<uint64_t>(c);
            pathHash *= 1099511628211ULL;
        }
        return PropertyId{pathHash};
    }

    bool operator==(const PropertyId &other) const { return id == other.id; }
    bool operator!=(const PropertyId &other) const { return id != other.id; }
    bool operator<(const PropertyId &other) const { return id < other.id; }

    // For use in unordered containers
    struct Hash
    {
        size_t operator()(const PropertyId &pid) const { return std::hash<uint64_t>{}(pid.id); }
    };
};

// ============================================================================
// MARK: Render Context (Unified)
// ============================================================================

/**
 * @brief 修改记录 - 记录单个属性的修改
 */
struct RenderModificationRecord
{
    PropertyId      propId; ///< Unique property identifier
    const Property *prop = nullptr;
    std::string     propPath;     ///< Full property path (e.g., "transform.position.x")
    std::string     oldValueJson; ///< Optional: old value for undo
    std::string     newValueJson; ///< Optional: new value
};

/**
 * @brief 统一渲染上下文 - 属性信息和修改追踪
 *
 * 特性：
 * - O(1) 属性修改查询（通过 PropertyId）
 * - 自动路径追踪（通过 ScopedPath RAII）
 * - 预分配内存减少分配
 *
 * 使用示例：
 * ```cpp
 * RenderContext ctx;
 * ctx.beginInstance(materialPtr);  // 设置当前实例
 * renderReflectedType("Material", typeIndex, materialPtr, ctx, 0);
 *
 * // 高效查询特定属性是否修改
 * if (ctx.isModified("params")) {
 *     material->setParamsDirty();
 * }
 * if (ctx.isModified("diffuseTexture")) {
 *     material->setResourceDirty();
 * }
 * ```
 */
struct RenderContext
{
    // ========== Modification Tracking ==========
    std::vector<RenderModificationRecord> modifications;
    std::unordered_set<uint64_t>          modifiedPathHashes; ///< O(1) lookup by path hash

    // ========== Instance & Path Tracking ==========
    const void *currentInstance = nullptr; ///< Current root instance being rendered
    std::string currentPath;               ///< Current property path for nested access

    // ========== Constructors ==========
    RenderContext()
    {
        modifications.reserve(8);
        modifiedPathHashes.reserve(8);
    }

    // ========== Instance Management ==========
    /**
     * @brief 开始渲染实例，同时收集该实例的延迟修改
     * @param instance 被渲染的实例指针
     *
     * 自动从 DeferredModificationQueue 收集该实例的所有延迟修改，
     * 合并到 modifications 列表中，使 isModified() 等查询也能感知异步修改。
     */
    void beginInstance(const void *instance)
    {
        currentInstance = instance;
        currentPath.clear();

        // Collect deferred modifications for this instance
        collectDeferredModifications();
    }

    /**
     * @brief 收集当前实例的延迟修改
     * 从全局 DeferredModificationQueue 获取并添加到 modifications
     */
    void collectDeferredModifications()
    {
        if (!currentInstance) return;

        // auto deferred = DeferredModificationQueue::get().collectForInstance(currentInstance);
        // for (const auto &dm : deferred) {
        //     PropertyId pid = PropertyId::make(currentInstance, dm.propPath);
        //     modifications.push_back({
        //         .propId       = pid,
        //         .prop         = nullptr, // Deferred modifications don't have Property pointer
        //         .propPath     = dm.propPath,
        //         .oldValueJson = "",
        //         .newValueJson = "[async]", // Marker for async modification
        //     });
        //     modifiedPathHashes.insert(PropertyId::fromPath(dm.propPath).id);
        // }
    }

    // ========== Modification Recording ==========
    void addModification(const Property *prop, const std::string &propName,
                         const std::string &oldVal = "", const std::string &newVal = "")
    {
        std::string fullPath = currentPath.empty() ? propName : (currentPath + "." + propName);
        PropertyId  pid      = PropertyId::make(currentInstance, fullPath);

        modifications.push_back({
            .propId       = pid,
            .prop         = prop,
            .propPath     = fullPath,
            .oldValueJson = oldVal,
            .newValueJson = newVal,
        });
        modifiedPathHashes.insert(PropertyId::fromPath(fullPath).id); // Store path-only hash for generic queries
    }

    // current propPath is modified, after the ScopedPath
    void pushModified(const std::string &oldVal = "", const std::string &newVal = "")
    {
        if (currentPath.empty()) return;
        modifications.push_back({
            .propId       = PropertyId::make(currentInstance, currentPath),
            .prop         = nullptr, // Marked as modified, no Property pointer needed
            .propPath     = currentPath,
            .oldValueJson = oldVal,
            .newValueJson = newVal,
        });

        modifiedPathHashes.insert(PropertyId::fromPath(currentPath).id);
    }

    // ========== Modification Query API ==========

    /// Check if any modifications occurred
    bool hasModifications() const
    {
        return !modifications.empty();
    }

    /// Check if a specific property path was modified (O(1) lookup)
    /// @param propPath Property path relative to current instance (e.g., "params", "diffuseTexture")
    bool isModified(const std::string &propPath) const
    {
        return modifiedPathHashes.contains(PropertyId::fromPath(propPath).id);
    }

    /// Check if any property starting with prefix was modified
    /// @param pathPrefix Path prefix to match (e.g., "params" matches "params.ambient", "params.diffuse")
    bool isModifiedPrefix(const std::string &pathPrefix) const
    {
        for (const auto &mod : modifications) {
            if (mod.propPath.starts_with(pathPrefix)) {
                return true;
            }
        }
        return false;
    }

    /// Check if a specific property (by PropertyId) was modified
    bool isModified(PropertyId pid) const
    {
        for (const auto &mod : modifications) {
            if (mod.propId == pid) {
                return true;
            }
        }
        return false;
    }

    /// Get all modifications matching a path prefix
    std::vector<const RenderModificationRecord *> getModifications(const std::string &pathPrefix = "") const
    {
        std::vector<const RenderModificationRecord *> result;
        for (const auto &mod : modifications) {
            if (pathPrefix.empty() || mod.propPath.starts_with(pathPrefix)) {
                result.push_back(&mod);
            }
        }
        return result;
    }

    void clear()
    {
        modifications.clear();
        modifiedPathHashes.clear();
        currentPath.clear();
        currentInstance = nullptr;
    }

    // ========== Path Management (RAII helper) ==========
    class ScopedPath
    {
      public:
        ScopedPath(RenderContext &ctx, const std::string &segment)
            : _ctx(ctx), _previousPath(ctx.currentPath)
        {
            if (ctx.currentPath.empty()) {
                ctx.currentPath = segment;
            }
            else {
                ctx.currentPath += "." + segment;
            }
        }
        ~ScopedPath()
        {
            _ctx.currentPath = std::move(_previousPath);
        }
        // Non-copyable
        ScopedPath(const ScopedPath &)            = delete;
        ScopedPath &operator=(const ScopedPath &) = delete;

      private:
        RenderContext &_ctx;
        std::string    _previousPath;
    };
};

// ============================================================================
// MARK: Type Renderer
// ============================================================================

/**
 * @brief 类型渲染器 - 定义特定类型的 ImGui 渲染逻辑
 *
 * 职责：
 * - 封装类型的 ImGui 渲染函数
 * - 接收实例指针、属性缓存上下文、修改追踪上下文作为参数
 *
 * 注意：不持有任何状态，只是纯函数封装
 * 
 * @param instance 实例指针
 * @param propCtx 属性缓存上下文（只读，描述如何渲染）
 * @param ctx 修改追踪上下文（可写，记录哪些属性被修改）
 */
struct TypeRenderer
{
    // RenderFunc now receives both PropertyRenderContext (readonly cache) and RenderContext (modification tracking)
    using RenderFunc = std::function<void(void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx)>;

    std::string typeName;
    RenderFunc  renderFunc = nullptr;

    void render(void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx) const
    {
        if (renderFunc) {
            renderFunc(instance, propCtx, ctx);
        }
    }
};

// ============================================================================
// MARK: Type Render Registry (Singleton)
// ============================================================================

/**
 * @brief 类型渲染器注册表 - 管理所有类型的渲染器
 *
 * 职责：
 * - 注册类型渲染器
 * - 查询类型渲染器
 * - 提供默认渲染器
 */
class TypeRenderRegistry
{
  public:
    static TypeRenderRegistry &instance();

    void registerRenderer(uint32_t typeIndex, TypeRenderer renderer)
    {
        _renderers[typeIndex] = renderer;
    }

    const TypeRenderer *getRenderer(uint32_t typeIndex) const
    {
        auto it = _renderers.find(typeIndex);
        return it != _renderers.end() ? &it->second : nullptr;
    }

    void clear() { _renderers.clear(); }

  private:
    TypeRenderRegistry() = default;
    std::unordered_map<uint32_t, TypeRenderer> _renderers;
};

// ============================================================================
// MARK: Type Rendering Functions
// ============================================================================

/**
 * @brief 递归渲染反射类型
 * @param name 显示名称
 * @param typeIndex 类型索引
 * @param instance 实例指针
 * @param ctx 渲染上下文，用于记录修改（通过 ctx.hasModifications() 查询）
 * @param depth 递归深度
 * @param propCtx 可选的属性渲染上下文
 *
 * @note 不再返回 bool，改用 ctx.isModified() 系列方法查询修改状态
 */
void renderReflectedType(const std::string &name, uint32_t typeIndex, void *instance, RenderContext &ctx, int depth = 0, const PropertyRenderContext *propCtx = nullptr);

/**
 * @brief 注册内置类型渲染器（int, float, string, vec3, vec4 等）
 */
void registerBuiltinTypeRenderers();

void pathWrapper(void *instance, const PropertyRenderContext &propCtx, RenderContext &ctx, auto internal);



bool renderPathPicker(std::string path, const std::string &typeName, auto internal)
{
    bool modified = false;

    // Display current path
    std::string displayPath = path.empty() ? "[No Path]" : path;

    // Path input field (read-only display)
    ImGui::Text("%s:", typeName.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-80); // Leave space for button

    char buffer[256];
    strncpy(buffer, displayPath.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    if (ImGui::InputText(("##" + typeName).c_str(), buffer, sizeof(buffer))) {
        path     = buffer;
        modified = true;
    }

    // Browse button - access FilePicker through App::get()->_editorLayer
    ImGui::SameLine();
    if (ImGui::Button(("Browse##" + typeName).c_str())) {
        internal(path);
    }

    return modified;
}

struct EditorLayer *getEditor();
struct FilePicker  *getFilePicker();

} // namespace ya
