---
name: ya-cpp-style
description: YA Engine C++ 编码约定与内存管理
---

## 适用场景

- 新增或重构 C++ 类/方法
- 代码 review 风格一致性检查
- 生命周期不清晰导致潜在崩溃

## 命名规范

- 类/结构体：PascalCase
- 私有成员：`_memberName`
- 公有成员：`memberName`
- 局部变量/函数：camelCase
- 常量：UPPER_SNAKE_CASE

## 不可变优先（Rust 风格）

所有变量**默认 `const`**，仅在需要修改时去掉。

```cpp
// ✅ Good
const auto  resolvedCS = resolveColorSpace(filepath, colorSpace);
const bool  bSRGB      = (resolvedCS == ETextureColorSpace::SRGB);
const auto& cacheKey   = getOrBuildCacheKey(filepath);
const auto  it         = _textureViews.find(cacheKey);

// ❌ Bad — 只读变量却不加 const
auto resolvedCS = resolveColorSpace(filepath, colorSpace);
bool bSRGB      = (resolvedCS == ETextureColorSpace::SRGB);
```

### 参数与返回值

- **值传递**仅用于 `sizeof(T) <= sizeof(uintptr_t)` 的基本类型（`bool`、`int`、`float`、`uint64_t`、枚举、裸指针）
- 其余一律 `const T&` 传入、`const T&` 返回（若数据在缓存/成员中有稳定生命周期）
- 需要调用方持有独立副本时才值返回，并加注释说明意图

```cpp
// ✅ 返回 cache 中的引用，零拷贝
const AssetMeta& getOrLoadMeta(const std::string& assetPath);
const std::string& getOrBuildCacheKey(const std::string& filepath);
const std::string& getString(const std::string& key, const std::string& defaultValue) const;

// ❌ 返回值拷贝 — 调用方只读时浪费堆分配
AssetMeta getOrLoadMeta(const std::string& assetPath);
std::string getString(const std::string& key, const std::string& defaultValue) const;
```

## 避免不必要的内存分配

### 禁止热路径上的临时拷贝

每帧调用的函数中，严禁拷贝 `std::string`、`nlohmann::json`、`AssetMeta` 等堆分配类型。

```cpp
// ❌ 每帧拷贝整个 AssetMeta（含 nlohmann::json 堆内存）
AssetMeta meta = getOrLoadMeta(filepath);         // getOrLoadMeta 返回 const& 却拷贝了

// ✅ 绑定到 const 引用
const auto& meta = getOrLoadMeta(filepath);
```

### 缓存查找结果应返回引用

当缓存查找命中时，不要拷贝值再返回，直接返回引用。

```cpp
// ❌ 拷贝 cached string
std::string cacheKey;
auto it = _cache.find(key);
if (it != _cache.end()) {
    cacheKey = it->second;   // 堆分配
}

// ✅ 提取 helper 返回引用
const std::string& getOrBuildCacheKey(const std::string& filepath);
// 调用方：
const auto& cacheKey = getOrBuildCacheKey(filepath);
```

### Map 操作：避免重复查找

```cpp
// ❌ 两次 map 查找
_resourceVersion[assetPath]++;
YA_CORE_TRACE("...", _resourceVersion[assetPath]);

// ✅ 一次查找
const auto newVersion = ++_resourceVersion[assetPath];
YA_CORE_TRACE("...", newVersion);
```

### 后缀检查：用 `ends_with` 不要 `substr`

```cpp
// ❌ 分配临时 string
metaPath.substr(metaPath.size() - suffix.size()) != suffix

// ✅ 零分配
!metaPath.ends_with(suffix)
```

## 消除重复代码

- 多处相同的查找/构建逻辑 → 提取 private helper
- 多个 MaterialComponent 中相同的模式 → 提取共享 utility 或用模板/回调泛化
- 判断标准：3 处以上相同逻辑 ≥ 5 行 → 必须提取

## 设计与内存规则

1. 类成员变量优先集中在类顶部，先关注数据组织。
2. 生命周期不明确时，禁止 `new/delete`，统一智能指针。
3. 接口返回优先 `shared_ptr`，避免裸指针悬空。
4. 常用别名：`stdptr<T>`、`makeshared<T>(...)`。
5. 单例优先采用 Meyers' Singleton，但 `instance()` 实现必须放在 `.cpp`，头文件只保留声明，避免跨 DLL 因头内联而产生多实例。

```cpp
// Header
class FooRegistry
{
  public:
    static FooRegistry& instance();
};

// Cpp
FooRegistry& FooRegistry::instance()
{
    static FooRegistry registry;
    return registry;
}
```

## 变更约束

- 做最小必要改动，不引入无关重构。
- 注释克制：避免无价值解释性注释。

## 退出条件

- 代码通过构建，且命名/指针策略符合项目约定
- 所有只读变量标记 `const`
- 热路径无不必要的堆分配
