#pragma once

// DLL export/import macros
#ifdef _WIN32
    #ifdef LAZYSTATIC_EXPORTS
        #define LAZYSTATIC_API __declspec(dllexport)
    #else
        #define LAZYSTATIC_API __declspec(dllimport)
    #endif
#else
    #define LAZYSTATIC_API
#endif

template <typename T>
struct LazyStatic
{
    static LAZYSTATIC_API T *_instance;

    static T *get()
    {
        return _instance;
    }
    static void init()
    {
        _instance = new T();
    }

  protected:
    LazyStatic()  = default;
    ~LazyStatic() = default;
};

// TODO("dose this have dll linkage issues? need test");
// template <typename T>
// inline T *LazyStatic<T>::_instance = nullptr;

#define LAZY_STATIC_IMPL(Type) \
    template <>                \
    LAZYSTATIC_API Type *LazyStatic<Type>::_instance = nullptr;
