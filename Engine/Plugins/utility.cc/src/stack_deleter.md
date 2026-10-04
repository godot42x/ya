# 栈式指针管理器 (StackDeleter) 使用指南

## 问题背景

在C++中，当我们需要管理多个动态分配的对象并按照特定顺序（通常是创建的逆序）进行清理时，传统的类型擦除方法（如将指针转换为void*）会导致析构函数不被正确调用。

## 解决方案

`StackDeleter` 类使用 `std::function` 来存储类型化的删除器，确保每个对象的正确析构函数被调用，同时保持类型擦除的便利性。

## 核心特性

1. **类型安全的删除**: 即使进行了类型擦除，也能正确调用析构函数
2. **LIFO（后进先出）顺序**: 按照栈的顺序清理资源，确保依赖关系正确
3. **异常安全**: 在析构过程中捕获异常，避免程序崩溃
4. **灵活的使用方式**: 支持自动类型推导和自定义清理函数

## 使用示例

### 基本用法

```cpp
#include "Core/StackDeleter.h"

void example()
{
    Neon::StackDeleter deleter;
    
    // 添加需要管理的指针
    auto* windowProvider = new SDLWindowProvider();
    deleter.push("WindowProvider", windowProvider);
    
    auto* device = new VulkanDevice();
    deleter.push("Device", device);
    
    // 当 deleter 析构时，会按照相反顺序删除：
    // 1. 先删除 device
    // 2. 再删除 windowProvider
}
```

### 自动类型名称

```cpp
void example_auto_naming()
{
    Neon::StackDeleter deleter;
    
    auto* obj1 = new MyClass();
    auto* obj2 = new AnotherClass();
    
    // 使用类型名称作为调试名称
    deleter.push(obj1);  // 名称将是 "MyClass" 或类似
    deleter.push(obj2);  // 名称将是 "AnotherClass" 或类似
}
```

### 自定义清理函数

```cpp
void example_custom_cleanup()
{
    Neon::StackDeleter deleter;
    
    // 添加自定义清理逻辑
    deleter.push_custom("Database Connection", []() {
        // 自定义清理逻辑
        cleanup_database_connection();
    });
    
    // 添加需要特殊处理的资源
    auto* resource = acquire_special_resource();
    deleter.push_custom("Special Resource", [resource]() {
        release_special_resource(resource);
    });
}
```

### 在App类中的应用

```cpp
// App.h
class App
{
    Neon::StackDeleter deleteStack;
    WindowProvider* windowProvider = nullptr;
    LogicalDevice* logicalDevice = nullptr;
    
public:
    void init();
    // ...
};

// App.cpp
void App::init()
{
    // 创建窗口提供者
    windowProvider = new SDLWindowProvider();
    deleteStack.push("SDLWindowProvider", windowProvider);
    windowProvider->init();

    // 创建逻辑设备（依赖于窗口提供者）
    logicalDevice = new VulkanDevice();
    deleteStack.push("LogicalDevice", logicalDevice);
    logicalDevice->init(LogicalDevice::InitParams{
        .bVsync = true,
        .windowProvider = *windowProvider,
    });
    
    // 当App析构时，会按照正确顺序清理：
    // 1. 先清理 logicalDevice（它依赖于 windowProvider）
    // 2. 再清理 windowProvider
}
```

## 技术实现

### 核心原理

1. **类型保持**: 使用模板函数在编译时捕获类型信息
2. **类型擦除**: 将删除逻辑包装在 `std::function<void()>` 中
3. **正确析构**: 在lambda中调用正确的 `delete` 操作符

```cpp
template<typename T>
void push(const std::string& name, T* ptr)
{
    if (ptr) {
        items_.emplace_back(name, [ptr, name]() {
            delete ptr;  // 调用T的析构函数
        });
    }
}
```

### 与传统方法的比较

**传统的错误方法：**
```cpp
void* ptr = new MyClass();
// ...
delete static_cast<MyClass*>(ptr);  // 如果类型信息丢失，可能出错
```

**StackDeleter的正确方法：**
```cpp
auto* ptr = new MyClass();
deleter.push("MyClass", ptr);  // 类型信息在编译时被捕获
// 析构时会正确调用 MyClass 的析构函数
```

## 最佳实践

1. **尽早注册**: 在对象创建后立即注册到 StackDeleter
2. **描述性名称**: 使用有意义的名称便于调试
3. **异常安全**: StackDeleter 会处理析构过程中的异常
4. **RAII原则**: 让 StackDeleter 的析构函数自动处理清理

## 注意事项

1. **非拷贝**: StackDeleter 不支持拷贝，只支持移动
2. **单次删除**: 每个指针只应该注册一次
3. **生命周期**: 确保 StackDeleter 的生命周期覆盖所有管理的对象
