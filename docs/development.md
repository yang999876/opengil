# openGil 开发手册

openGil 的长期目标是把 `.gil` 的可编辑结构尽可能拆清楚，并把每一个已经验证的能力沉淀成稳定的 C++ op、CLI 命令和 Python Binding。这个过程适合开源协作：一个贡献者可以只研究一个字段、一个参数、一个对象行为，只要实验链路清楚、验证充分，就能成为项目的一部分。

命令用法见 `docs/capabilities.md`。面向 agent 的调用规则见 `skills/gil-editing/`。

## 1. 贡献者路线图

新贡献者推荐按这个顺序进入项目：

1. 先跑通构建和测试。
2. 用 `opengil inspect`、`validate`、`list-*` 命令熟悉一个样本 `.gil`。
3. 选择一个很小的研究目标，例如“某个物体的一个参数如何修改”。
4. 用 before/after 样本定位变化字段。
5. 写一个最小 replay 或 op，让 openGil 能复现游戏编辑器里的变化。
6. 把修改后的 `.gil` 放回游戏验证。
7. 换一个新存档或新对象做交叉验证。
8. 补齐测试、原始样本和 PR 说明。

这个项目接受小步 PR。一次 PR 只解决一个清晰能力，会比一次性提交一大片猜测更容易合并，也更方便后来的人复查。

## 2. 环境和标准检查

构建：

```powershell
cmake --build build --config Release
```

运行测试：

```powershell
ctest --test-dir build -C Release --output-on-failure
```

常用冒烟检查：

```powershell
.\build\Release\opengil.exe inspect --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe validate --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe list-prefabs --input .\tests\fixtures\test1.gil
```

JSON 边界检查：

```powershell
rg "result_json" include src/ops src/semantic
rg "_to_json" include/opengil
rg '#include "opengil/json.hpp"' src/ops src/semantic
```

后三条命令应该没有输出。CLI JSON formatter 统一放在 `src/cli/json_formatters.hpp/.cpp`。

## 3. 项目分层

```text
include/opengil/      公开 C++ 头文件，定义数据结构和操作接口
src/core/             .gil envelope、wire parser、rebuild、sha256、JSON 小工具
src/semantic/         只读语义查询
src/ops/              原子写操作，返回 bytes/payload/summary
src/cli/              CLI 参数、写入策略、stdout JSON、exit code
src/python/           pybind11 绑定，提供内存态 GilDocument
tests/                C++ unit、CLI smoke、Python binding 测试
docs/                 面向用户和贡献者的公开文档
skills/gil-editing/   agent 调用 openGil 的规则和参考资料
```

分层职责：

- `core` 处理底层字节、wire 格式、文件 envelope、结构校验。
- `semantic` 负责读取和查询，返回结构化数据。
- `ops` 负责修改，返回 mutation bytes 和结构化 summary。
- `cli` 负责命令行参数、文件写入、JSON 输出、exit code、dry-run、report。
- Python Binding 复用 `semantic` 和 `ops`，在内存态 `GilDocument` 上连续应用操作。

库层保持和 CLI JSON 解耦：

- `include/opengil/*` 暴露结构和函数，不暴露 `*_to_json()`。
- `src/ops/*` 和 `src/semantic/*` 返回结构化结果。
- mutation 类型携带 bytes 和 summary。
- CLI 输出格式集中维护在 `src/cli/json_formatters.hpp/.cpp`。

## 4. `.gil` 研究工作流

研究一个未知能力时，以“单变量实验”为原则。每轮实验只改变一个物体、一个参数或一个编辑动作。

标准流程：

1. 在游戏里准备一个空场景，保存为基线存档。
2. 新增物体 1，保存为 `gil1.gil`。
3. 只修改物体 1 的目标参数，保存为 `gil2.gil`。
4. 用 openGil 检查两个样本。
5. 用 `diff-summary` 定位变化的 top-level field。
6. 继续解析变化字段，找到稳定的 nested path 和语义含义。
7. 按项目现有风格编写只读查询、replay 代码或写 op。
8. 用 openGil 生成修改后的 `.gil`。
9. 把生成的 `.gil` 放回游戏，确认目标行为真的发生变化。
10. 换一个新存档或新对象，再做一次交叉验证。
11. 把实验过程、原始 `.gil` 样本、测试结果放进 PR。

推荐命令：

```powershell
.\build\Release\opengil.exe inspect --input .\research\case-name\gil1.gil
.\build\Release\opengil.exe inspect --input .\research\case-name\gil2.gil
.\build\Release\opengil.exe validate --input .\research\case-name\gil1.gil
.\build\Release\opengil.exe validate --input .\research\case-name\gil2.gil
.\build\Release\opengil.exe diff-summary --before .\research\case-name\gil1.gil --after .\research\case-name\gil2.gil --report .\research\case-name\diff-summary.json
```

实验目录建议：

```text
research/<feature-name>/
  README.md
  gil1.gil
  gil2.gil
  cross-gil1.gil
  cross-gil2.gil
  diff-summary.json
  notes.md
```

`README.md` 记录实验目标、游戏内操作步骤、目标对象 id、修改参数、预期变化和验证结论。`notes.md` 可以记录字段路径推导过程、失败尝试和后续问题。

## 5. 字段确认标准

一个字段从“观察到变化”进入“可以写代码”的状态，需要满足这些条件：

- before/after 样本只包含一个明确编辑动作。
- `diff-summary` 能稳定指向一组变化字段。
- nested path 有清楚解释，例如 `top5.field1 -> object transform`。
- 代码生成的 `.gil` 能通过 `validate`。
- 代码生成的 `.gil` 放回游戏后能产生预期变化。
- 同一能力在第二个存档或第二个对象上通过交叉验证。
- PR 里附带原始测试 `.gil` 文件和复现步骤。

如果研究对象涉及 mirror 结构，例如 prefab、scene、preview 同步记录，验证时要同时检查所有相关位置。比如修改 prefab 模型时，需要确认 prefab 本体、scene 实例和 preview 实例都符合预期。

## 6. 新增写操作

新增写操作推荐按这个顺序实现：

1. 在 `include/opengil/<area>_ops.hpp` 增加 summary、mutation 和函数声明。
2. 在 `src/ops/<area>_ops.cpp` 实现查找、局部 parse、修改、rebuild。
3. 在 `src/cli/json_formatters.hpp/.cpp` 增加 CLI formatter。
4. 在 `src/cli/main.cpp` 接入一个单次原子命令。
5. 在 `src/python/bindings.cpp` 增加 `GilDocument` 方法。
6. 增加 C++ unit test。
7. 按需要补 CLI smoke 或 Python binding 测试。
8. 更新 `docs/capabilities.md`，让公开文档和代码同步。

写操作安全规则：

- 写路径使用 `parse_owned_fields_or_throw(...)`。
- 保留未知字段、字段顺序和未修改 top-level bytes。
- 只 rebuild 被修改的 message 或 top-level field。
- 用户传入的 `--prefab-id`、`--object-id`、`--entry-id` 等 id 要做存在性或唯一性检查。
- CLI 写命令支持 `--dry-run`。
- `src/ops/*` 返回 bytes，由 CLI 或 Python `save()` 负责写文件。
- 测试至少重新 load mutation bytes 并运行 `validate_gil(...)`。
- 复杂操作检查关键语义字段、目标不存在、重复 id 等路径。

CLI 负责文件层安全写入。多次修改优先用 Python Binding 的 `GilDocument` 在内存中组合，最后统一保存。

## 7. 新增只读查询

只读查询推荐放在 `src/semantic/semantic.cpp` 或对应专用 semantic 文件中：

1. 在 `include/opengil/semantic.hpp` 或专用头文件增加结果结构。
2. 在 semantic 层实现扫描和结构化返回。
3. 在 CLI formatter 和 `src/cli/main.cpp` 接命令。
4. 在 Python Binding 中增加 `GilDocument` 方法。
5. 增加 fixture-backed unit 或 CLI smoke test。
6. 更新 `docs/capabilities.md`。

只读查询是研究路径里的第一层沉淀。一个字段还没有达到可写标准时，也可以先提交清晰的只读能力，帮助后续贡献者继续拆解。

## 8. 测试要求

每个写操作至少覆盖：

- 输入 `.gil` 可以 load。
- mutation bytes 可以重新 load。
- `validate_gil(...)` 通过。
- summary 里关键 id、字段数量或 changed top fields 符合预期。
- 修改后的语义查询结果符合预期。

有 CLI 命令时，补一条 CLI smoke 测试。暴露 Python Binding 时，补 Python 测试或扩展 `tests/python/test_python_bindings.py`。

研究型 PR 还要附带游戏内验证结果：

- 第一次验证：原始 before/after 样本。
- openGil replay：由代码生成的 `.gil`。
- 游戏验证：把 replay 文件放回游戏后的结果说明。
- 交叉验证：第二个存档或第二个对象上的同能力验证。

## 9. PR 提交要求

提交 PR 时请包含这些内容：

- 研究目标：这次拆解的是哪个 `.gil` 能力或字段。
- 游戏步骤：如何从空场景得到 `gil1.gil` 和 `gil2.gil`。
- 原始样本：附带 `gil1.gil`、`gil2.gil`，以及交叉验证使用的 `.gil` 文件。
- 差分结果：附带 `diff-summary.json` 或贴出 changed top fields。
- 字段结论：说明 top-level field、nested path 和语义解释。
- 实现说明：新增或修改了哪些 op、semantic query、CLI、Python Binding。
- 验证结果：结构校验、单元测试、CLI/Python 测试、游戏内验证、交叉验证。
- 文档更新：对应更新 `docs/capabilities.md` 或本开发手册。

建议 PR 描述模板：

```markdown
## 研究目标

## 游戏内复现步骤

## 样本文件

## 差分结论

## 实现说明

## 验证结果

## 后续问题
```

## 10. 已验证能力的记录方式

已经进入代码的能力，需要同步维护三处信息：

- `docs/capabilities.md`：用户如何调用。
- `docs/development.md`：贡献者如何验证和扩展。
- `skills/gil-editing/references/verified-operations.md`：agent 使用的已验证写入面。

记录已验证能力时，优先写清楚“修改哪些字段、同步哪些镜像结构、测试覆盖哪些路径”。这能让后续贡献者继续沿着同一套证据链推进。

## 11. 常见工作流示例

先研究，再实现：

```powershell
.\build\Release\opengil.exe diff-summary --before .\research\scale\gil1.gil --after .\research\scale\gil2.gil
.\build\Release\opengil.exe list-scene-objects --input .\research\scale\gil1.gil
```

实现后做 dry-run：

```powershell
.\build\Release\opengil.exe <new-command> --input .\research\scale\gil1.gil --output .\research\scale\replay.gil --dry-run
```

写出 replay 文件并验证：

```powershell
.\build\Release\opengil.exe <new-command> --input .\research\scale\gil1.gil --output .\research\scale\replay.gil
.\build\Release\opengil.exe validate --input .\research\scale\replay.gil
```

运行自动化检查：

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
