#pragma once

#include "Core/Base.h"
#include "Core/Trait.h"
#include <memory>
#include <mutex>

template <typename T>
struct LazyStatic : disable_copy
{
    static T *_instance;

    static T *get()
    {
        return _instance;
    }

  protected:
    LazyStatic()  = default;
    ~LazyStatic() = default;
};

TODO("dose this have dll linkage issues? need test");
template <typename T>
inline T *LazyStatic<T>::_instance = nullptr;

