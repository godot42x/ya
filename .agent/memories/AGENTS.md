# Memory Index

只有在以下情况才读取 memory：

- 当前问题像历史回归或老坑重现
- 需要查之前的调试结论、局部约定、踩坑记录
- 需要决定某个结论该进 skill 还是 memory

## 当前条目

- `./animation_system_debug.md`：骨骼动画、shadow pass、root transform、shader define 相关历史问题
- `./vulkan_submit_lifecycle_debug.md`：Vulkan / MoltenVK submit 期生命周期与 keepalive 排查
- `./ibl_visual_regression_baseline.md`：IBL / environment lighting 视觉回归的固定观察基线

## 边界

- 可复用的系统设计、长期工作流、稳定约定写入 `../skills/*/SKILL.md`
- 一次性故障、回归根因、项目历史坑写入 `./*.md`
