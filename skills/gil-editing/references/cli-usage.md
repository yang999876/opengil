# CLI Usage For Agents

`opengil` 的能力总览见仓库根目录的 `docs/capabilities.md`。
完整 CLI 参数和示例见 `docs/cli_api.md`，Python Binding 见 `docs/python_api.md`。
这个文件只保留 agent 调用时最容易出错的规则。

## Default Flow

```powershell
opengil inspect --input input.gil
opengil validate --input input.gil
opengil <write-command> --input input.gil --output output.gil --dry-run
opengil <write-command> --input input.gil --output output.gil
opengil validate --input output.gil
```

stdout 是机器可读 JSON envelope；成功和失败结果都从 stdout 读取。

## Rules

- 写操作前先 inspect 或运行相关 list/get 命令定位目标。
- 优先用 numeric id 写入，不靠名字猜目标。
- Windows shell 下非 ASCII tab 名优先用 `--tab-id`，不要依赖 `--tab`。
- 复杂写操作先 `--dry-run`，确认 summary 后再真正写出。
- 覆盖原文件必须显式传 `--in-place`；默认使用 `--output`。
- 写后必须 `validate`，但不要把结构 validate 当成完整语义证明。
- 未知结构不要猜。准备 before/after `.gil`，先跑 `diff-summary`。
- 多次修改优先用 Python binding 的内存态 `GilDocument` 组合。

## Common Commands

```powershell
opengil list-prefabs --input input.gil
opengil list-scene-objects --input input.gil
opengil list-preview-objects --input input.gil
opengil get-model --input input.gil --prefab-id 1077936130
opengil set-model --input input.gil --output output.gil --prefab-id 1077936130 --asset-id 20001220
opengil attachment from-decoration --input input.gil --output output.gil --prefab-id 1077936385
```
