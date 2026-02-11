#pragma once

#include "Handle.h"

#include "Render/RenderDefines.h"

namespace ya
{

// 图像切面标志（RHI层）
namespace EImageAspect
{
enum T : uint32_t
{
    None    = 0,
    Color   = 1 << 0, // 颜色数据
    Depth   = 1 << 1, // 深度数据
    Stencil = 1 << 2, // 模板数据

    // 常用组合
    DepthStencil = Depth | Stencil,

    // 通用标志
    Plane0 = 1 << 3, // 多平面图像的第一平面
    Plane1 = 1 << 4, // 多平面图像的第二平面
    Plane2 = 1 << 5, // 多平面图像的第三平面
};
}

// 图像视图类型（RHI层）
namespace EImageViewType
{
enum T : uint32_t
{
    View1D        = 0, // 1D纹理
    View2D        = 1, // 2D纹理
    View3D        = 2, // 3D纹理
    ViewCube      = 3, // 立方体贴图
    View1DArray   = 4, // 1D纹理数组
    View2DArray   = 5, // 2D纹理数组
    ViewCubeArray = 6, // 立方体贴图数组
};
}

// 图像创建标志（RHI层）
namespace EImageCreateFlag
{
enum T : uint32_t
{
    None            = 0,
    CubeCompatible  = 1 << 0, // 支持立方体贴图
    MutableFormat   = 1 << 1, // 可变格式
    SparseBinding   = 1 << 2, // 稀疏绑定
    SparseResidency = 1 << 3, // 稀疏驻留
    SparseAliased   = 1 << 4, // 稀疏别名
    Protected       = 1 << 5, // 受保护图像
    ExtendedUsage   = 1 << 6, // 扩展使用
    Disjoint        = 1 << 7, // 不相关平面

    // 特定用途标志
    ForCubeMap = CubeCompatible, // 用于立方体贴图
};
}

struct ImageHandleTag
{};
using ImageHandle = Handle<ImageHandleTag>;

// ImageViewHandle is already defined in DescriptorSet.h
// Forward declare if needed
struct ImageViewHandleTag;
using ImageViewHandle = Handle<ImageViewHandleTag>;



struct IImageView;

struct IImage : public plat_base<IImage>
{
    // std::vector<stdptr<IImageView>> _views; // imageview managed by the image?
    // IImageView* getDefaultImageView() {}// create one ?
  protected:
    IImage() = default;

  public:

    /**
     * @brief Get the platform-specific handle for this image
     * @return ImageHandle Platform handle (e.g., VkImage for Vulkan)
     */
    virtual ImageHandle getHandle() const = 0;

    /**
     * @brief Get the width of the image
     * @return uint32_t Image width in pixels
     */
    virtual uint32_t getWidth() const = 0;

    /**
     * @brief Get the height of the image
     * @return uint32_t Image height in pixels
     */
    virtual uint32_t getHeight() const = 0;

    /**
     * @brief Get the format of the image
     * @return EFormat::T Image format
     */
    virtual EFormat::T getFormat() const = 0;

    /**
     * @brief Get the usage flags of the image
     * @return EImageUsage::T Image usage flags
     */
    virtual EImageUsage::T getUsage() const = 0;

    /**
     * @brief Get the current layout of the image
     * @return EImageLayout::T Current image layout
     */
    virtual EImageLayout::T getLayout() const = 0;

    /**
     * @brief Set the debug name for this image (for debugging tools)
     * @param name Debug name string
     */
    virtual void setDebugName(const std::string &name) = 0;

    // TODO: getImageInfo, getSubresourceLayout etc.
};

struct IImageView : public plat_base<IImageView>
{
  protected:
    std::shared_ptr<IImage> _image;

  public:
    IImageView()                              = default;
    IImageView(const IImageView &)            = default;
    IImageView &operator=(const IImageView &) = default;
    IImageView(IImageView &&)                 = default;
    IImageView &operator=(IImageView &&)      = default;
    ~IImageView() override                    = default;

    /**
     * @brief Get the platform-specific handle for this image view
     * @return ImageViewHandle Platform handle (e.g., VkImageView for Vulkan)
     */
    virtual ImageViewHandle getHandle() const = 0;

    /**
     * @brief Get the underlying image
     */
    const IImage *getImage() const { return _image.get(); }

    virtual EFormat::T getFormat() const { return EFormat::Undefined; }

    virtual void setDebugName(const std::string &name) = 0;
};

} // namespace ya
