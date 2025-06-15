
#pragma once

#include "WindowProvider.h"
#include "reflect.cc/enum"



enum class ESamplerType
{
    DefaultLinear = 0,
    DefaultNearest,
    PointClamp,
    PointWrap,
    LinearClamp,
    LinearWrap,
    AnisotropicClamp,
    AnisotropicWrap,
    ENUM_MAX,
};
GENERATED_ENUM_MISC(ESamplerType);

struct LogicalDevice
{

    void *nativeDevice = nullptr;
    void *nativeWindow = nullptr;

    virtual ~LogicalDevice() { NE_CORE_TRACE("LogicalDevice::~LogicalDevice()"); }

    struct InitParams
    {
        bool            bVsync = true;
        WindowProvider &windowProvider;
    };

    virtual bool init(const InitParams &params) = 0;

    template <typename DeviceType>
    DeviceType *getNativeDevicePtr() { return static_cast<DeviceType *>(nativeDevice); } // TODO: check type safety
    template <typename WindowType>
    WindowType *getNativeWindowPtr() { return static_cast<WindowType *>(nativeWindow); } // TODO: check type safety
};
