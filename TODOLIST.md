# 待办

## 像素画 Decoration 导入

- 为 `pixel-art import-decoration` 增加显式 tab 选择，优先支持 `--tab-id`，让生成的像素画 prefab 能进入指定 prefab tab，而不是依赖当前 `create_prefab` 的默认 category 映射。
- 等真实的可着色方块 asset id 明确后，替换当前占位用的 decoration `--asset-id` 流程。
- 等可着色方块的颜色字段路径 / 组件格式确认后，补上 decoration 颜色写入。
- 等 decoration 颜色行为确认后，决定非不透明 PNG 像素应该跳过、按可见像素处理，还是映射到未来的颜色 / 透明度字段。
