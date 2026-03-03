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

## 设计与内存规则

1. 类成员变量优先集中在类顶部，先关注数据组织。
2. 生命周期不明确时，禁止 `new/delete`，统一智能指针。
3. 接口返回优先 `shared_ptr`，避免裸指针悬空。
4. 常用别名：`stdptr<T>`、`makeshared<T>(...)`。

## 变更约束

- 做最小必要改动，不引入无关重构。
- 注释克制：避免无价值解释性注释。

## 退出条件

- 代码通过构建，且命名/指针策略符合项目约定
