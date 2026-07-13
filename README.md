# TLVView — 轻量零拷贝 TLV 编解码库

TLVView 是一个轻量、零拷贝的 TLV（Tag-Length-Value）二进制协议编解码库，单头文件实现，兼容 C++11。

## 协议格式

### TLV 简介

TLV（Tag-Length-Value）是一种自描述的二进制编码格式，每个数据单元由三部分组成：

- **Tag**：标签，标识本字段的类型/含义
- **Length**：长度，说明紧随其后的 Value 占用多少字节
- **Value**：值，实际承载的数据

这种"类型-长度-值"的结构使得数据可被顺序、自洽地解析——读取 Tag 知道是什么，读取 Length 知道 Value 在哪里结束，从而跳过未知字段、定位目标字段。TLV 广泛用于网络协议、智能卡（EMV）、ASN.1（BER/DER）等场景。

### 本库的字段定义

```
+------+--------+------------+
| Tag  | Length |   Value    |
| 1 B  |  4 B   |   N B      |
+------+--------+------------+
  ^       ^          ^
 type    N bytes    payload
```

| 字段   | 类型       | 宽度   | 字节序         | 说明                                            |
| ------ | ---------- | ------ | -------------- | ----------------------------------------------- |
| Tag    | `uint8_t`  | 1 字节 | —              | 节点类型标识（可配置，见[类型配置](#类型配置)） |
| Length | `uint32_t` | 4 字节 | **Big-Endian** | Value 的字节数 N                                |
| Value  | —          | N 字节 | 视节点类型     | 节点数据                                        |

> Tag 与 Length 的宽度可调整，详见[类型配置](#类型配置)小节。上表为默认配置。

### Value 的两种形态

Value 依据 Tag 对应的节点类型，承载不同内容：

- **值类型节点**（`NodeUint32` / `NodeBinary` / `NodeString`）：Value 是定长或变长的原始数据。例如 uint32 节点的 Value 是 4 字节 Big-Endian 整数，String 节点的 Value 是字符串字节序列。
- **容器节点**（`NodeObject`）：Value 是**若干子 TLV 节点的顺序拼接**，形成树状嵌套结构。子节点之间无分隔符，靠各自的 Length 划界。

### 嵌套结构示例

一个 Object（tag=0x10）含两个子节点：uint32（tag=0x01, value=0x12345678）和 string（tag=0x03, "hi"）：

```
Object node (tag=0x10), Value = child1 + child2 = 9 + 7 = 16 bytes

+------+------------+--------------------------------+
| Tag  | Length     |  Value (16 bytes)              |
+------+------------+--------------------------------+
| 0x10 | 0x00000010 |  [child1 9B] [child2 7B]       |
+------+------------+--------------------------------+

  Child 1: uint32 (tag=0x01, value=0x12345678)
  +----+----------+----------+
  |0x01|0x00000004|0x12345678|
  +----+----------+----------+

  Child 2: string (tag=0x03, "hi")
  +----+----------+------+
  |0x03|0x00000002|'h''i'|
  +----+----------+------+
```

解析时无需预先展开整棵树——Extractor 以零拷贝视图记录每层 Object 的 Value 起止指针，按 `GetChild` / 迭代器按需逐层下钻。

### 字节序

所有多字节整数（Length 字段、uint32 节点的 Value）均按**网络字节序（Big-Endian）**存储，与具体主机字节序无关，保证跨平台数据一致。本库以逐字节移位实现读写，不依赖 `htonl/ntohl` 等平台 API。

## 特性

- **单头文件**：仅需 `#include "TLVView.h"`，无外部依赖
- **零拷贝解析**：Extractor 以内存视图方式解析，不拷贝数据，按需渐进式访问子节点
- **无异常设计**：构造、解析与序列化均不抛异常，错误通过 `IsValid()` 与返回值表达，适合禁用异常的环境（详见[无异常设计](#无异常设计)）
- **模板化字节序处理**：`BufferWriteValue` / `BufferReadValue` 按类型特化，统一处理 BE 读写
- **类型安全**：Builder 侧每种节点类型独立封装；Extractor 侧通过 `AsNodeXxx` 显式转换
- **可配置类型**：`TLVView::TagType` / `TLVView::LengthType` 可在头文件命名空间内统一修改
- **range-for 遍历**：Object 节点支持迭代器，自然遍历未知 tag 的子节点

## 快速开始

### 编译运行示例

```bash
g++ -std=c++11 test.cpp -o test
./test
```

### 组装（Builder）

```cpp
#include "TLVView.h"
using namespace TLVView;

// 构造子节点（栈上创建，由 Object 持有指针）
Builder::NodeUint32  child1(0x01, 0x12345678);
Builder::NodeString  child2(0x03, "hello");
Builder::NodeUint32  child3(0x02, 0xCAFEBABE);

// Object 容器，模板参数为最大子节点数
Builder::NodeObject<8> obj(0x10);
obj.AddChild(&child1);
obj.AddChild(&child2);
obj.AddChild(&child3);

// 序列化为字节流（返回 MemoryBuffer，不抛异常）
MemoryBuffer data = obj.Serialize();
if (!data.IsValid()) { /* 内存分配失败处理 */ }
// data.data() / data.size() 访问字节流
```

### 解析（Extractor）

```cpp
#include "TLVView.h"
using namespace TLVView;

// 从字节流构造 Object 视图（零拷贝，仅记录指针与长度）
Extractor::NodeObject obj(data.data(), data.size());

// 按 tag 取子节点，返回具体类型节点；用 IsValid() 判断是否成功
Extractor::NodeUint32 u32 = obj.GetChildUint32(0x01);
if (u32.IsValid())
    printf("tag=0x01 value=0x%08X\n", u32.GetValue());

Extractor::NodeString str = obj.GetChildString(0x03);
if (str.IsValid())
    printf("tag=0x03 string=%s\n", str.GetString().c_str());

// 重复 tag，取第 N 个（0 基索引）
u32 = obj.GetChildUint32(0x02, 1);
if (u32.IsValid())
    printf("tag=0x02 (second): 0x%08X\n", u32.GetValue());

// range-for 遍历所有子节点（未知 tag 场景）
for (Extractor::NodeBase child : obj)
{
    printf("tag=0x%02X, length=%u\n", child.GetTag(), (unsigned)child.GetLength());
    // 按需转换：child.AsNodeUint32() / AsNodeString() / ...
}
```

## API 参考

### TLVView::Builder（组装）

所有节点继承自 `NodeBase`，需实现 `CalcTotalSize()` / `Write()` / `IsValid()`。构造函数不抛异常，参数非法（如 `NodeBinary` 的 length 超过 `VALUE_MAX_LENGTH`）时进入无效态。

| 类              | 说明                                                         |
| --------------- | ------------------------------------------------------------ |
| `NodeObject<N>` | Object 容器，`N` 为最大子节点数；通过 `AddChild(NodeBase*)` 添加子节点，槽位满时返回 false |
| `NodeUint32`    | uint32 值节点，`NodeUint32(tag, value)`                      |
| `NodeBinary`    | 二进制节点，`NodeBinary(tag, data, length)`；length 超限时无效 |
| `NodeString`    | 字符串节点（继承 NodeBinary），`NodeString(tag, "str")`      |

通用方法：

- `Serialize()` → `MemoryBuffer`：序列化整个节点树，内部分配内存。**不抛异常**，分配失败时返回 `IsValid()==false` 的空对象（用 `data()` / `size()` 访问字节流）
- `Serialize(uint8_t* buf, size_t bufLen)` → `bool`：序列化到外部 buffer，空间不足返回 false。适合调用方自行管理内存的场景
- `GetTag()` → `TLVView::TagType`：获取 tag
- `IsValid()` → `bool`：节点是否有效

**MemoryBuffer**（`Serialize()` 的返回类型，仅支持移动语义）：

- `IsValid()` → `bool`：内存分配是否成功
- `data()` / `size()`：数据指针与字节数
- 析构自动释放内存；不可拷贝，可 `std::move` 转移所有权

### TLVView::Extractor（解析）

所有节点继承自 `NodeBase`，持有指向 Value 的指针和长度，不拷贝数据。构造函数不抛异常，数据不完整或格式错误时进入无效态。

**NodeBase 通用方法：**

- `IsValid()` → `bool`：构造是否成功（buffer 完整且声明长度未越界）
- `GetTag()` / `GetValue()` / `GetLength()`：获取解析出的 tag、value 指针、value 长度
- `AsNodeUint32()` / `AsNodeBinary()` / `AsNodeString()` / `AsNodeObject()`：视图转换为具体类型

**NodeObject 方法：**

按 tag 取子节点，返回对应类型节点；未找到或数据不完整时返回无效态节点（`IsValid()==false`），通过方法名区分类型：

- `GetChildUint32(tag, index=0)` → `NodeUint32`
- `GetChildBinary(tag, index=0)` → `NodeBinary`
- `GetChildString(tag, index=0)` → `NodeString`
- `GetChildObject(tag, index=0)` → `NodeObject`
- `GetChildAt(index)` → `NodeBase`：按序号取第 N 个子节点（不按 tag 过滤），用于未知 tag 的顺序访问
- `GetChildCount()` → `size_t`：实时统计子节点数量
- `begin()` / `end()`：range-for 迭代器，遍历所有子节点

**各值节点：**

- `NodeUint32::GetValue()` → `uint32_t`；`IsValid()` 额外校验 value 长度 ≥ 4
- `NodeBinary`：通过继承的 `GetValue()` / `GetLength()` 获取原始数据指针与长度
- `NodeString::GetStringView()` → `StringView`（零拷贝）/ `GetString()` → `std::string`（拷贝）

## 设计说明

### 双子命名空间

- **`TLVView::Builder`**：组装侧，节点对象拥有数据，`Serialize()` 产出字节流。子节点以裸指针挂载到 `NodeObject`，调用方需保证子节点生命周期覆盖 Object 的 `Serialize()` 调用。
- **`TLVView::Extractor`**：解析侧，零拷贝内存视图。仅记录 buffer 指针与长度，不预解析子节点，`GetChildXxx` 按需渐进式解析。引用的 buffer 须在使用期间保持有效。构造与解析均不抛异常，数据不完整时返回 `IsValid()==false` 的无效态节点。

### 无异常设计

本库全程不使用异常（`throw` / `try-catch`），包括内存分配。错误通过返回值与 `IsValid()` 表达：

- **Builder 侧**：`AddChild` / `Serialize(buf, len)` 以 `bool` 返回值表达成功与否；构造参数非法时节点进入无效态，`IsValid()` 返回 false。
- **Extractor 侧**：构造函数解析失败（buffer 过短、声明长度越界、value 长度不足等）时进入无效态；`GetChildXxx` 找不到目标子节点时返回 `IsValid()==false` 的节点。
- **内存分配**：`Serialize()` 返回自定义的 `MemoryBuffer` 而非 `std::vector`，内部用 `operator new(std::nothrow)` 分配，失败时返回 `IsValid()==false` 的空对象，不抛 `std::bad_alloc`。

```cpp
MemoryBuffer data = obj.Serialize();
if (!data.IsValid()) { /* 内存分配失败，不抛异常 */ }
// data.data() / data.size() 访问字节流，析构自动释放
```

需要自行管理内存的场景，仍可用外部 buffer 重载（`Serialize(buf, len) → bool`）。

库可在禁用异常的编译环境（`-fno-exceptions`）与异常开销敏感的嵌入式/高性能场景中使用。

> **为何不用 `std::vector`？** `std::vector` 构造在内存不足时会抛 `std::bad_alloc`，这是标准库的固有行为，无法在不引入 `try/catch`（依赖异常编译选项）的前提下屏蔽。本库用轻量的 `MemoryBuffer`（仅构造/析构/`data()`/`size()`，支持移动、禁拷贝）替代，使无异常承诺完整自洽。

### 字节序处理

`TLVView` 命名空间提供 `BufferWriteValue<T>` / `BufferReadValue<T>` 模板特化（uint8/uint16/uint32），以逐字节移位实现 Big-Endian 读写，零平台依赖、对未对齐访问安全，编译期常量折叠。

### 类型配置

Tag 与 Length 的字段类型在 [TLVView.h](TLVView.h) 的 `namespace TLVView` 内通过 `using` 声明配置：

```cpp
namespace TLVView {
    // Tag 与 Length 的字段类型配置
    using TagType    = uint8_t;    // Tag 字段类型
    using LengthType = uint32_t;   // Length 字段类型
    ...
}
```

调用方以 `TLVView::TagType` / `TLVView::LengthType` 引用。

**可选值与影响：**

| 类型                  | 可选值                              | 字段宽度       | 说明                  |
| --------------------- | ----------------------------------- | -------------- | --------------------- |
| `TLVView::TagType`    | `uint8_t` / `uint16_t` / `uint32_t` | 1 / 2 / 4 字节 | 决定可表示的 tag 数量 |
| `TLVView::LengthType` | `uint8_t` / `uint16_t` / `uint32_t` | 1 / 2 / 4 字节 | 决定 Value 最大长度   |

修改后无需改动其他代码——`TAG_LENGTH` / `LEN_LENGTH` / `VALUE_MAX_LENGTH` 等常量及所有 `BufferWriteValue` / `BufferReadValue` 读写逻辑均通过模板特化自动适配。

> **注意**：`BufferWriteValue` / `BufferReadValue` 当前仅特化了 `uint8_t` / `uint16_t` / `uint32_t` 三种宽度。若将 `TLVView::LengthType` 设为其他类型（如 `uint64_t`），需在 [TLVView.h](TLVView.h) 的 `TLVView` 命名空间内补充对应特化。Tag 与 Length 类型须从这三种已有特化中选取。

#### 模板参数 vs 全局 `using` 对比

本库用命名空间内的 `using` 定义 Tag/Length 类型，而非把节点类设计成模板（`NodeObject<TagT, LenT, N>`）。两者对比：

| 维度             | 全局 `using`（当前） | 模板参数                 |
| ---------------- | -------------------- | ------------------------ |
| 单协议场景适用性 | ✅ 足够               | ✅ 但冗余                 |
| 同程序多协议支持 | ⚠️ 需复制文件（见下） | ✅ 原生支持               |
| 代码复杂度       | ✅ 低                 | ❌ 高（所有节点类模板化） |
| 使用书写         | ✅ 简洁               | ❌ 冗长，需别名缓解       |
| 类型层面防混用   | ❌ 无                 | ✅ 不同配置不同类型       |
| 性能             | =                    | =（编译期常量折叠一致）  |

模板参数的全部理论优势（多协议、类型区分）建立在"同一程序需要多种 TLV 配置"这一前提上。而 TLV 协议的本质特征是**协议一旦定义即固定**——Tag/Length 宽度是协议规约的一部分，不会运行时变化。这与通用容器（`std::vector<T>` 必须模板化）有本质区别。工业界 TLV/ASN.1 库通常也固定编码规则类型。

因此本库选择 `using` 定义：在典型单协议场景下更简洁，代价是"多协议同程序"需借助下述复制文件方式，而非天然支持。这是"为不会发生的需求避免支付复杂度成本"。

#### 定义多种 TLV 格式的方法

当一个程序需要同时处理多种不同宽度的 TLV 协议时，无需改造本库为模板。解决办法：**复制并重命名头文件，修改命名空间即可**。

步骤：

1. 复制 `TLVView.h` 为 `TLVView2.h`
2. 在 `TLVView2.h` 中把 `namespace TLVView` 全部改为 `namespace TLVView2`
3. 修改 `TLVView2.h` 中的 `using TagType` / `using LengthType` 为第二种协议的宽度
4. 同时 `#include` 两个头文件，分别用 `TLVView::` 和 `TLVView2::` 前缀访问

```cpp
#include "TLVView.h"
#include "TLVView2.h"

TLVView::Builder::NodeObject<8>  objA(0x10);   // 协议A：tag 1B, len 4B
TLVView2::Builder::NodeObject<8> objB(0x20);   // 协议B：tag 2B, len 2B
```

**为何不冲突？** 类型别名 `TagType`/`LengthType` 及所有节点类都定义在各自命名空间内（`TLVView::` 与 `TLVView2::`），分属不同命名空间，可同程序共存。这正是本库把 `using` 收进命名空间（而非放全局）的原因——放全局会因同名 `using` 重定义而冲突，收进命名空间则天然隔离。

> 若需更多协议，同理复制 `TLVView3.h`、`TLVView4.h`……每个文件一个独立命名空间。代价是代码重复，但对"协议固定、种类有限"的场景，这比模板化整个库更简单直接。

## 兼容性

- C++11 及以上
- 已验证：VS2013、g++ 13.2
- 无第三方依赖，仅使用标准库
- 无异常设计：构造、解析与内存分配均不抛异常，可在禁用异常（`-fno-exceptions`）的环境中使用

## 目录结构

```
.
├── TLVView.h      # 核心头文件（Builder + Extractor）
├── test.cpp       # 示例与测试
├── CMakeLists.txt # CMake 构建配置
├── LICENSE        # MIT License
└── README.md
```

## 许可证

本项目基于 [MIT License](LICENSE) 开源，可自由使用、修改、分发。
