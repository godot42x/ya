# GUI Framework 重构 Session Checklist

## 开工

- [ ] 阅读根 AGENTS.md、本计划 plan.md、progress.md。
- [ ] 检查 git status --short，确认不覆盖用户已有改动。
- [ ] 本轮只处理一个 feature 或一个 Phase 子目标。
- [ ] 读取对应 skill：GUI 改动用 gui-framework，构建/测试用 ya-build。
- [ ] 运行目标相关的最小 baseline 测试。
- [ ] 确认是否涉及生成文件、GPU 资源生命周期或 frame boundary。

## 实现中

- [ ] 先更新 contract/test，再实现 framework。
- [ ] 保持 authored appearance 与 optional theme 的优先级一致。
- [ ] runtime visual mutation 使用 changed-only setter 或 mutation transaction。
- [ ] Layout 影响只触发 Layout；颜色/brush 只触发 Paint。
- [ ] snapshot/compose 不读取 live WidgetTree 或业务对象。
- [ ] 纹理、字体、descriptor 等资源保活到 submit 完成。

## 收尾

- [ ] 运行 closure/unit/scenario 测试。
- [ ] 改渲染时执行 windowed/offscreen/GPU shot 验收。
- [ ] 清理临时日志、调试代码和生成文件手改。
- [ ] 更新 progress.md。
- [ ] 更新 feature_matrix.json，只修改状态，不删除 feature。
- [ ] 记录失败测试、剩余风险和下一步。
- [ ] 按 [gui] phase N: description 提交；plan 工件与本轮代码同一提交。
