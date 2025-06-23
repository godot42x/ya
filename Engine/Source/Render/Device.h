
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

    void           *nativeDevice   = nullptr;
    WindowProvider *windowProvider = nullptr;

    virtual ~LogicalDevice() { NE_CORE_TRACE("LogicalDevice::~LogicalDevice()"); }

    struct InitParams
    {
        bool            bVsync = true;
        WindowProvider &windowProvider;
    };

    virtual bool init(const InitParams &params) = 0;
    virtual void destroy()                      = 0;

    template <typename DeviceType>
    DeviceType *getNativeDevicePtr() { return static_cast<DeviceType *>(nativeDevice); }
};
