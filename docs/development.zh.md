# openGil 开发说明

这份中文文档是当前开发说明的主文档。英文版 `docs/development.md`
是较短的参考版，不是逐句翻译；如果两者表达详略不同，以这份中文文档为准。

这份文档是给后续维护者和 agent 看的，也给你自己整理项目现状用。它的重点不是解释每一行代码，而是回答几个实际问题：

- 现在这个项目处在什么状态？
- 每个目录负责什么？
- 后面要接一个新功能，应该从哪里下手？
- 哪些安全规则不能破坏？
- 测试应该怎么补？

## 当前状态

openGil 是一个独立的 C++20 CLI 工具，用来读取、检查、修改已经验证过的 `.gil` 文件结构。

它现在已经可以作为内部工具和 agent 工具使用，但还不建议把它当成稳定公开版。比较准确的定位是：

```text
openGil v0.1 usable internal release
```

也就是说：

- 你自己用：可以。
- 让 agent 调用：可以。
- 继续在上面开发功能：可以。
- 公开给别人当稳定工具：还需要再补文档、样例和更多 golden 测试。

现在最推荐的安全使用流程是：

```powershell
.\build\Release\opengil.exe inspect --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe <写操作> --input in.gil --output out.gil --dry-run
.\build\Release\opengil.exe <写操作> --input in.gil --output out.gil
.\build\Release\opengil.exe validate --input out.gil
```

注意：`validate` 现在是结构校验，不是完整语义校验。

它会检查：

- `.gil` 文件 envelope 大小是否对得上
- payload 能不能按 protobuf wire 格式解析

它不会完整证明：

- id 一定没有重复
- prefab、scene、preview 一定完全同步
- tab 映射一定完整
- 没有悬空引用

所以写操作本身仍然要负责自己的语义安全。

## 项目分层

现在项目大致分成这几层：

```text
include/opengil/      公开 C++ 头文件，定义数据结构和操作接口
src/core/             .gil 文件读写、wire parser、rebuild、sha256、JSON 小工具
src/semantic/         只读语义查询，比如 tabs、prefabs、models、nodegraphs
src/ops/              写操作，也就是各种原子修改能力
src/cli/              CLI 参数解析、文件写入策略、stdout JSON 输出
```

最重要的边界是：

- `core` 只处理底层字节、wire 格式、文件 envelope、结构校验。
- `semantic` 只做读取和查询，返回结构化数据。
- `ops` 做修改，返回结构化 summary。
- `cli` 负责命令行参数、文件写入、JSON 输出、exit code、batch、dry-run。

一个非常重要的新规则是：

```text
库层不负责 JSON 输出。
```

也就是说：

- `include/opengil/*` 里不应该暴露 `*_to_json()`。
- `src/ops/*` 不应该拼 JSON 字符串。
- mutation 类型不应该携带 `result_json`。
- CLI 如果要输出 JSON，统一放到 `src/cli/json_formatters.cpp`。

现在 CLI JSON formatter 在这里：

```text
src/cli/json_formatters.hpp
src/cli/json_formatters.cpp
```

这样做的好处是：以后如果要做 C++ library 或 Python binding，不会被 CLI JSON 格式绑死。

## 重要文件地图

底层核心：

```text
src/core/gil.cpp       .gil envelope 解析、构建、结构 validate
src/core/wire.cpp      protobuf wire parser、field rebuild、字段工具
src/core/sha256.cpp    CLI report 里用到的 sha256
src/core/json_value.cpp batch ops.json 用的小型 JSON parser
```

语义查询：

```text
src/semantic/semantic.cpp
```

这里现在负责：

- list tabs
- list prefabs
- prefab tabs
- get model
- list nodegraphs

写操作：

```text
src/ops/model_ops.cpp                  set-model / set-empty-model
src/ops/prefab_ops.cpp                 rename/delete/clone/copy prefab
src/ops/object_ops.cpp                 create object、create prefab、transform
src/ops/nodegraph_ops.cpp              attach nodegraph
src/ops/projectile_ops.cpp             projectile motion
src/ops/custom_vars_ops.cpp            custom vars add/remove/copy/sync
src/ops/decoration_ops.cpp             decoration add
src/ops/attachment_ops.cpp             attachment add
src/ops/attachment_from_decoration_ops.cpp 从 decoration 推导 attachment
src/ops/ui_ops.cpp                     UI primitive list
src/ops/ui_patch_ops.cpp               UI primitive 属性修改
src/ops/ui_structure_ops.cpp           UI append/retain/copy structure
```

CLI：

```text
src/cli/main.cpp             命令解析、dispatch、写入策略、batch
src/cli/json_formatters.cpp  CLI JSON result 输出
```

`main.cpp` 现在比较大，但它是有意先放回一个文件的。以后如果要拆，应该按真实职责拆，比如：

- 参数解析
- 文件写入策略
- batch parser
- command handlers

不要再拆一个只有转发意义的 `app.cpp`。

## 写操作的安全规则

写 `.gil` 的时候，最怕的问题不是“报错”，而是“悄悄写坏”。所以新增或修改写操作时，要遵守这些规则。

### 1. 写路径必须 strict parse

写操作应该使用：

```cpp
parse_owned_fields_or_throw(...)
```

不要在写操作里裸用：

```cpp
parse_owned_fields(...)
```

因为后者解析失败会返回空结果，适合只读扫描，不适合写入。写操作要 fail closed，也就是不能确定安全就直接失败。

### 2. 保留未知字段和字段顺序

`.gil` 里有大量未知结构。openGil 的策略不是重建完整 IR，而是：

- 只解析需要修改的局部结构
- 未知字段原样保留
- 原字段顺序尽量保持
- 只 rebuild 被修改的 message 或 top-level field

### 3. 显式 id 必须查重

如果用户传了类似：

```powershell
--prefab-id
--object-id
--entry-id
```

操作必须先检查这个 id 有没有被占用。不能直接相信用户输入。

### 4. 写操作必须支持 dry-run

CLI 写操作都应该支持：

```powershell
--dry-run
```

dry-run 不写文件，只输出 summary。复杂操作给 agent 用时应该优先 dry-run。

### 5. ops 层不要直接写文件

`src/ops/*` 只返回新的 bytes 和 summary。

真正写文件由 CLI 层负责，这样才能统一处理：

- `--output`
- `--in-place`
- 原子写入
- report
- exit code

### 6. 写后测试至少 validate

每个写操作的测试，至少应该把 mutation bytes 重新 load 成 `.gil`，然后跑：

```cpp
validate_gil(...)
```

更好的测试还应该检查关键语义字段确实变了。

## 如何新增一个写操作

大多数新功能都按这个流程来。

### 第一步：设计公开接口

在对应的头文件里加 summary 和函数声明，比如：

```text
include/opengil/example_ops.hpp
```

summary 应该是结构化字段，不是 JSON 字符串。

示例：

```cpp
struct ExampleSummary {
  uint64_t prefab_id = 0;
  std::string prefab_name;
  std::vector<uint32_t> changed_top_fields;
};

struct ExampleMutation {
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> payload;
  ExampleSummary summary;
};
```

### 第二步：实现 op

在：

```text
src/ops/example_ops.cpp
```

里面实现真正的 `.gil` 修改。

这一层不要：

- 解析 CLI 参数
- 拼 JSON
- 写文件
- 打印日志

这一层只做：

- 找目标
- parse 局部 message
- 修改字段
- rebuild
- 返回 mutation summary

### 第三步：加 CLI JSON formatter

在：

```text
src/cli/json_formatters.hpp
src/cli/json_formatters.cpp
```

加一个 formatter。

CLI 输出字段要尽量稳定，因为这是 agent 的兼容层。

### 第四步：接 CLI handler

在：

```text
src/cli/main.cpp
```

加命令解析和 dispatch。

一个写命令大致应该做这些事：

1. 读取 `--input`
2. 解析参数
3. 调用 `src/ops` 的函数
4. 如果不是 `--dry-run`，写入 `--output`
5. 用 formatter 输出标准 envelope JSON
6. 支持 `--report`

### 第五步：接 batch

如果这个操作很可能被 agent 批量调用，就应该接入 batch。

需要改：

- batch op 数据结构
- batch JSON parser
- batch dispatch
- batch 测试 fixture

agent 做大量修改时，应该优先生成 `ops.json`，然后一次调用：

```powershell
opengil batch --input in.gil --output out.gil --ops ops.json
```

不要让 agent 反复启动 CLI、反复解析同一个 `.gil`。

### 第六步：加测试

写操作测试至少覆盖：

- summary 字段正确
- changed top fields 正确
- mutation 后 validate 通过
- 关键语义字段确实改变
- 目标不存在时失败
- 显式 id 重复时失败，如果这个操作涉及 id

如果输出稳定，最好加 golden 测试。

## 如何新增一个只读查询

只读查询一般放在：

```text
src/semantic/semantic.cpp
```

流程是：

1. 在 `include/opengil/semantic.hpp` 或对应头文件里加结果结构。
2. 在 `src/semantic/semantic.cpp` 实现扫描。
3. 在 `src/cli/json_formatters.cpp` 加 JSON 输出。
4. 在 `src/cli/main.cpp` 加 CLI 命令。
5. 加 fixture 测试。

只读查询可以比写操作宽容一点，但不要把解析失败伪装成“安全可写”。

## 标准检查命令

每次做完比较大的改动，建议跑：

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

JSON 解耦相关的静态检查：

```powershell
rg "result_json" include src/ops src/semantic
rg "_to_json" include/opengil
rg '#include "opengil/json.hpp"' src/ops src/semantic
```

这三条应该没有输出。

常用 CLI smoke test：

```powershell
.\build\Release\opengil.exe list-prefabs --input .\tests\fixtures\test1.gil
.\build\Release\opengil.exe set-model --input .\tests\fixtures\test1.gil --prefab-id 1086324737 --asset-id 20001220 --dry-run
.\build\Release\opengil.exe custom-vars add --input .\tests\fixtures\test1.gil --prefab-id 1086324737 --name openGilVar --type str --dry-run
.\build\Release\opengil.exe batch --input .\tests\fixtures\test1.gil --ops .\tests\fixtures\batch-model-rename.json --dry-run
```

## Agent Skill 的关系

agent skill 在：

```text
skills/gil-editing/
```

skill 的原则应该是：

- 优先调用 `opengil`
- 不默认 dump 巨大 JSON IR
- 写操作先 inspect，复杂操作先 dry-run
- 优先使用 id，不靠名字写入
- 写后必须 validate
- 批量修改优先生成 `ops.json` 调 `batch`
- 未知结构不要猜，要走 before/after diff

如果一个功能已经在 CLI 里稳定实现，skill 应该调用 CLI，而不是重新写脚本解析 `.gil`。

## 继续研究未知结构的流程

对未知结构不要直接写 generalized editor。推荐流程是：

1. 准备 before `.gil`
2. 在游戏或编辑器里做一个最小修改
3. 得到 after `.gil`
4. 做 before/after diff
5. 找出最小变化路径
6. 先做 replay-first 操作
7. 再抽象成可配置 op
8. 加 fixture 和测试

这是为了避免“看起来能写，实际破坏未知字段”的情况。

## 当前已知限制

- 这是 pre-1.0 项目，C++ public API 还可以破坏性调整。
- `validate` 不是完整语义校验。
- `.proto` 文件只是文档/参考，不用于无损写回。
- 暂时不做 Python `.pyd`，但当前架构保留了未来 pybind11 包装的空间。
- `ui import-geometrize` 和 `ui import-pixel` 这轮明确不做。
- `src/cli/main.cpp` 还比较大，但当前先保持一个文件，后续要按真实职责拆。
