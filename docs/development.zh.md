# openGil 开发说明

这份文档只服务维护者：说明项目边界、写操作安全规则、如何新增能力和怎么验证。
命令用法放在 `docs/capabilities.zh.md`，agent 调用规则放在
`skills/gil-editing/`。

## 当前定位

openGil 是一个 C++20 CLI + Python binding，用来读取、检查、修改已经验证过
的 `.gil` 文件结构。CLI JSON 是 agent 兼容层；C++ public API 和 Python
binding 仍按 pre-1.0 项目处理，可以随实现演进调整。

推荐安全流程：

```powershell
.\build\Release\opengil.exe inspect --input in.gil
.\build\Release\opengil.exe <写操作> --input in.gil --output out.gil --dry-run
.\build\Release\opengil.exe <写操作> --input in.gil --output out.gil
.\build\Release\opengil.exe validate --input out.gil
```

`validate` 只做结构校验：检查 `.gil` envelope 和 protobuf wire payload。
它不证明 id 唯一、tab 映射完整、scene/preview 镜像完整或没有悬空引用。

## 项目分层

```text
include/opengil/      公开 C++ 头文件，定义数据结构和操作接口
src/core/             .gil envelope、wire parser、rebuild、sha256、JSON 小工具
src/semantic/         只读语义查询
src/ops/              原子写操作，返回 bytes/payload/summary
src/cli/              CLI 参数、写入策略、stdout JSON、exit code
src/python/           pybind11 绑定，提供内存态 GilDocument
tests/                C++ unit、CLI smoke、Python binding 测试
skills/gil-editing/   agent 调用 openGil 的规则和参考资料
```

边界规则：

- `core` 只处理底层字节、wire 格式、文件 envelope、结构校验。
- `semantic` 只做读取和查询，返回结构化数据。
- `ops` 做修改，返回结构化 summary，不解析 CLI、不写文件、不拼 JSON。
- `cli` 负责命令行参数、文件写入、JSON 输出、exit code、dry-run、report。
- Python binding 复用 `ops`/`semantic`，在内存态 `GilDocument` 上连续应用操作。

## JSON 边界

库层不负责 CLI JSON 输出：

- `include/opengil/*` 不暴露 `*_to_json()`。
- `src/ops/*` 和 `src/semantic/*` 不拼 JSON 字符串。
- mutation 类型不携带 `result_json`。
- CLI JSON formatter 统一放在 `src/cli/json_formatters.hpp/.cpp`。

这能保持 C++ library、Python binding 和 CLI 输出互不绑死。

## 写操作安全规则

写 `.gil` 最怕悄悄写坏。新增或修改写操作时遵守这些规则：

1. 写路径使用 `parse_owned_fields_or_throw(...)`，不要用宽松解析伪装成功。
2. 保留未知字段、字段顺序和未修改 top-level bytes。
3. 只 rebuild 被修改的 message 或 top-level field。
4. 用户显式传入的 `--prefab-id`、`--object-id`、`--entry-id` 等必须查重。
5. CLI 写命令必须支持 `--dry-run`。
6. `src/ops/*` 不直接写文件；写入统一由 CLI 或 Python document save 处理。
7. 写操作测试至少重新 load mutation bytes 并运行 `validate_gil(...)`。
8. 复杂操作还要检查关键语义字段确实改变，以及目标不存在/重复 id 等失败路径。

## 新增写操作

推荐路线：

1. 在 `include/opengil/<area>_ops.hpp` 增加结构化 summary、mutation 和函数声明。
2. 在 `src/ops/<area>_ops.cpp` 实现查找、局部 parse、修改、rebuild。
3. 在 `src/cli/json_formatters.hpp/.cpp` 增加 CLI formatter。
4. 在 `src/cli/main.cpp` 接入一个清晰的单次原子命令。
5. 如果脚本组合有价值，在 `src/python/bindings.cpp` 增加 `GilDocument` 方法。
6. 增加 unit test；必要时补 CLI smoke 或 Python binding 测试。

CLI 不承担批处理编排。多次修改优先用 Python binding 的内存态
`GilDocument` 组合，最后统一保存。

## 新增只读查询

只读查询通常放在 `src/semantic/semantic.cpp`：

1. 在 `include/opengil/semantic.hpp` 或已有专用头文件加结果结构。
2. 在 semantic 层实现扫描。
3. 在 CLI formatter 和 `main.cpp` 接命令。
4. 如果 Python 侧需要，绑定到 `GilDocument`。
5. 加 fixture-backed unit 或 CLI smoke test。

只读扫描可以比写操作宽容，但不能把会影响写安全的解析失败隐藏掉。

## 标准检查

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

JSON 解耦检查：

```powershell
rg "result_json" include src/ops src/semantic
rg "_to_json" include/opengil
rg '#include "opengil/json.hpp"' src/ops src/semantic
```

后三条应没有输出。

## Agent Skill

如果功能已在 CLI 稳定实现，skill 应调用 `opengil`，不要重新写脚本解析
`.gil`。未知结构仍走 before/after diff、replay-first、再抽象成 op 的流程。

## 已知限制

- pre-1.0 项目，C++ public API 和 Python binding 都还可能调整。
- `validate` 不是完整语义校验。
- `.proto` 文件只是文档/参考，不用于无损写回。
- `ui import-geometrize` 和 `ui import-pixel` 当前不实现。
- `src/cli/main.cpp` 仍较大，后续要按真实职责拆，而不是拆薄转发层。
