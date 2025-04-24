#pragma once

    struct Window{

        void * nativeWindowHandle = nullptr;

        template <typename T>
        T *getNativeWindowPtr() { return static_cast<T *>(nativeWindowHandle); } // TODO: check type safety
    };