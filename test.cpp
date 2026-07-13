#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include "TLVView.h"

using namespace TLVView;

// ---------------------------------------------------------------------------
// 测试框架：CHECK_* 宏累计失败数，每条断言打印实际值与预期对比。
// 重要：actual 只求值一次（存入临时变量），避免 AddChild 等带副作用的
// 表达式被重复求值导致状态被多次修改。
// ---------------------------------------------------------------------------
static int g_failures = 0;
static int g_checks   = 0;

// 布尔断言：actual 求值一次后缓存。
#define CHECK_BOOL(actual, expect_true)                                        \
    do {                                                                        \
        ++g_checks;                                                             \
        bool _a = (actual) ? true : false;                                      \
        bool _e = (expect_true);                                                \
        if (_a == _e) {                                                         \
            printf("    [OK]  actual=%s expect=%s\n", _a?"true":"false", _e?"true":"false"); \
        } else {                                                                \
            ++g_failures;                                                       \
            printf("    [FAIL] actual=%s expect=%s\n", _a?"true":"false", _e?"true":"false"); \
        }                                                                       \
    } while (0)

#define CHECK_TRUE(actual)  CHECK_BOOL(actual, true)
#define CHECK_FALSE(actual) CHECK_BOOL(actual, false)

#define CHECK_EQ(actual, expect)                                              \
    do {                                                                        \
        ++g_checks;                                                             \
        int _a = (int)(actual);                                                 \
        int _e = (int)(expect);                                                 \
        if (_a == _e) {                                                         \
            printf("    [OK]  actual=%d expect=%d\n", _a, _e);                  \
        } else {                                                                \
            ++g_failures;                                                       \
            printf("    [FAIL] actual=%d expect=%d\n", _a, _e);                 \
        }                                                                       \
    } while (0)

#define CHECK_EQ_HEX(actual, expect)                                           \
    do {                                                                        \
        ++g_checks;                                                             \
        unsigned _a = (unsigned)(actual);                                      \
        unsigned _e = (unsigned)(expect);                                      \
        if (_a == _e) {                                                         \
            printf("    [OK]  actual=0x%08X expect=0x%08X\n", _a, _e);          \
        } else {                                                                \
            ++g_failures;                                                       \
            printf("    [FAIL] actual=0x%08X expect=0x%08X\n", _a, _e);        \
        }                                                                       \
    } while (0)

#define CHECK_STR(actual, expect) \
    do {                                                                        \
        ++g_checks;                                                             \
        std::string _a = (actual);                                              \
        std::string _e = (expect);                                              \
        if (_a == _e) {                                                         \
            printf("    [OK]  actual=\"%s\" expect=\"%s\"\n", _a.c_str(), _e.c_str()); \
        } else {                                                                \
            ++g_failures;                                                       \
            printf("    [FAIL] actual=\"%s\" expect=\"%s\"\n", _a.c_str(), _e.c_str()); \
        }                                                                       \
    } while (0)

// ---------------------------------------------------------------------------
// 构造测试 buffer：object(tag=0x10) 含 4 个子节点 + 其后一个独立 string 节点
//   子1: tag=0x01 uint32 0x12345678
//   子2: tag=0x02 uint32 0x87654321
//   子3: tag=0x03 string "hello"
//   子4: tag=0x02 uint32 0xCAFEBABE  (与子2重复 tag)
//   独立: tag=0x04 string "world"
// 返回 totalLen；objEnd 通过出参返回（独立节点起始偏移）
// ---------------------------------------------------------------------------
static size_t BuildSampleBuffer(uint8_t buffer[256], size_t* objEnd)
{
    size_t offset = 0;

    // 外层 object：tag=0x10
    buffer[offset++] = 0x10;
    size_t objLenPos = offset;
    offset += 4;
    size_t objStart = offset;

    // 子节点1：tag=0x01, uint32 value=0x12345678
    buffer[offset++] = 0x01;
    offset = BufferWriteValue(buffer + offset, (uint32_t)4) - buffer;
    offset = BufferWriteValue(buffer + offset, (uint32_t)0x12345678) - buffer;

    // 子节点2：tag=0x02, uint32 value=0x87654321
    buffer[offset++] = 0x02;
    offset = BufferWriteValue(buffer + offset, (uint32_t)4) - buffer;
    offset = BufferWriteValue(buffer + offset, (uint32_t)0x87654321) - buffer;

    // 子节点3：tag=0x03, string "hello"
    const char* str = "hello";
    size_t strLen = strlen(str);
    buffer[offset++] = 0x03;
    offset = BufferWriteValue(buffer + offset, (uint32_t)strLen) - buffer;
    memcpy(buffer + offset, str, strLen);
    offset += strLen;

    // 子节点4：tag=0x02, uint32 value=0xCAFEBABE（重复 tag=0x02）
    buffer[offset++] = 0x02;
    offset = BufferWriteValue(buffer + offset, (uint32_t)4) - buffer;
    offset = BufferWriteValue(buffer + offset, (uint32_t)0xCAFEBABE) - buffer;

    size_t objPayloadLen = offset - objStart;
    BufferWriteValue(buffer + objLenPos, (uint32_t)objPayloadLen);

    *objEnd = offset;

    // 单独节点5（在 object 之后）：tag=0x04, string "world"
    const char* str2 = "world";
    size_t strLen2 = strlen(str2);
    buffer[offset++] = 0x04;
    offset = BufferWriteValue(buffer + offset, (uint32_t)strLen2) - buffer;
    memcpy(buffer + offset, str2, strLen2);
    offset += strLen2;

    return offset;
}

// ---------------------------------------------------------------------------
// Test 1: 直接用 NodeString 解析独立 string 节点
// ---------------------------------------------------------------------------
static void TestDirectNodeString(const uint8_t* buffer, size_t objEnd, size_t totalLen)
{
    printf("--- Test 1: Direct NodeString ---\n");

    Extractor::NodeString node5(buffer + objEnd, totalLen - objEnd);
    CHECK_TRUE(node5.IsValid());
    CHECK_EQ(node5.GetTag(), 0x04);
    CHECK_EQ(node5.GetLength(), 5);

    StringView sv = node5.GetStringView();
    CHECK_EQ(sv.size(), 5);
    CHECK_STR(std::string(sv.data(), sv.size()), "world");

    std::string s = node5.GetString();
    CHECK_STR(s, "world");
}

// ---------------------------------------------------------------------------
// Test 1b: NodeBase 视图 + AsNodeXxx 转换链路
// ---------------------------------------------------------------------------
static void TestAsNodeConvert(const uint8_t* buffer, size_t objEnd, size_t totalLen)
{
    printf("\n--- Test 1b: NodeBase AsNodeXxx ---\n");

    Extractor::NodeBase base(buffer + objEnd, totalLen - objEnd);
    CHECK_TRUE(base.IsValid());
    CHECK_EQ(base.GetTag(), 0x04);
    CHECK_EQ(base.GetLength(), 5);

    Extractor::NodeString strNode = base.AsNodeString();
    CHECK_TRUE(strNode.IsValid());
    CHECK_STR(strNode.GetString(), "world");

    // object 的 tag=0x01 子节点用 GetChildUint32 间接验证 AsNode 链路
    Extractor::NodeObject obj(buffer, totalLen);
    Extractor::NodeUint32 u32 = obj.GetChildUint32(0x01);
    CHECK_TRUE(u32.IsValid());
    CHECK_EQ_HEX(u32.GetValue(), 0x12345678);
}

// ---------------------------------------------------------------------------
// Test 2: NodeObject + GetChildXxx(tag) 按 tag 取子节点
// ---------------------------------------------------------------------------
static void TestGetChildByTag(const uint8_t* buffer, size_t totalLen)
{
    printf("\n--- Test 2: NodeObject with GetChildXxx(tag) ---\n");

    Extractor::NodeObject obj(buffer, totalLen);
    CHECK_TRUE(obj.IsValid());
    CHECK_EQ(obj.GetChildCount(), 4);

    // tag=0x01
    Extractor::NodeUint32 u32 = obj.GetChildUint32(0x01);
    CHECK_TRUE(u32.IsValid());
    CHECK_EQ_HEX(u32.GetValue(), 0x12345678);

    // tag=0x02 第1个
    u32 = obj.GetChildUint32(0x02);
    CHECK_TRUE(u32.IsValid());
    CHECK_EQ_HEX(u32.GetValue(), 0x87654321);

    // tag=0x02 第2个（重复 tag）
    u32 = obj.GetChildUint32(0x02, 1);
    CHECK_TRUE(u32.IsValid());
    CHECK_EQ_HEX(u32.GetValue(), 0xCAFEBABE);

    // tag=0x02 第3个（不存在）—— 应返回无效态
    u32 = obj.GetChildUint32(0x02, 2);
    CHECK_FALSE(u32.IsValid());

    // tag=0x03 string
    Extractor::NodeString strChild = obj.GetChildString(0x03);
    CHECK_TRUE(strChild.IsValid());
    CHECK_STR(strChild.GetString(), "hello");

    // 不存在的 tag —— 应返回无效态
    Extractor::NodeUint32 notFound = obj.GetChildUint32(0xFF);
    CHECK_FALSE(notFound.IsValid());

    // range-for 遍历所有子节点（未知 tag 顺序遍历）
    // 预期顺序: 0x01(4) 0x02(4) 0x03(5) 0x02(4)
    static const struct { uint8_t tag; uint32_t len; } kExpect[] = {
        {0x01, 4}, {0x02, 4}, {0x03, 5}, {0x02, 4}
    };
    size_t idx = 0;
    for (Extractor::NodeBase child : obj)
    {
        char label[32];
        snprintf(label, sizeof(label), "child[%u] tag", (unsigned)idx);
        CHECK_EQ(child.GetTag(), kExpect[idx].tag);
        snprintf(label, sizeof(label), "child[%u] length", (unsigned)idx);
        CHECK_EQ(child.GetLength(), kExpect[idx].len);
        ++idx;
    }
    CHECK_EQ(idx, 4);
}

// ---------------------------------------------------------------------------
// Test 3: 嵌套 NodeObject 解析
//   outer(0x20) -> inner(0x21) -> uint32(0x05, 0xDEADBEEF)
// ---------------------------------------------------------------------------
static void TestNestedObject()
{
    printf("\n--- Test 3: Nested NodeObject ---\n");

    // 最内层 uint32
    uint8_t nested[64];
    size_t noff = 0;
    nested[noff++] = 0x05;
    noff = BufferWriteValue(nested + noff, (uint32_t)4) - nested;
    noff = BufferWriteValue(nested + noff, (uint32_t)0xDEADBEEF) - nested;
    size_t innerPayload = noff;

    // inner object
    uint8_t buf2[64];
    size_t o2 = 0;
    buf2[o2++] = 0x21;
    o2 = BufferWriteValue(buf2 + o2, (uint32_t)innerPayload) - buf2;
    memcpy(buf2 + o2, nested, innerPayload);
    o2 += innerPayload;
    size_t outerPayload = o2;

    // outer object
    uint8_t buf3[64];
    size_t o3 = 0;
    buf3[o3++] = 0x20;
    o3 = BufferWriteValue(buf3 + o3, (uint32_t)outerPayload) - buf3;
    memcpy(buf3 + o3, buf2, outerPayload);
    o3 += outerPayload;

    Extractor::NodeObject outer(buf3, o3);
    CHECK_TRUE(outer.IsValid());
    CHECK_EQ(outer.GetTag(), 0x20);
    CHECK_EQ(outer.GetChildCount(), 1);

    Extractor::NodeObject inner = outer.GetChildObject(0x21);
    CHECK_TRUE(inner.IsValid());
    CHECK_EQ(inner.GetTag(), 0x21);
    CHECK_EQ(inner.GetChildCount(), 1);

    Extractor::NodeUint32 val = inner.GetChildUint32(0x05);
    CHECK_TRUE(val.IsValid());
    CHECK_EQ_HEX(val.GetValue(), 0xDEADBEEF);
}

// ---------------------------------------------------------------------------
// Test 4: Builder 基本组装 + 往返验证
// ---------------------------------------------------------------------------
static void TestBuilderBasic()
{
    printf("--- Test 4: Builder basic serialize ---\n");

    Builder::NodeUint32 c1(0x01, 0x12345678);
    StringView sv("hello", 5);
    Builder::NodeString c2(0x03, sv);
    const uint8_t bin[] = { 0xAA, 0xBB, 0xCC };
    Builder::NodeBinary c3(0x04, bin, sizeof(bin));
    Builder::NodeUint32 c4(0x02, 0xCAFEBABE);

    Builder::NodeObject<8> obj(0x10);
    CHECK_TRUE(obj.AddChild(&c1));
    CHECK_TRUE(obj.AddChild(&c2));
    CHECK_TRUE(obj.AddChild(&c3));
    CHECK_TRUE(obj.AddChild(&c4));
    CHECK_TRUE(obj.IsValid());

    MemoryBuffer data = obj.Serialize();
    CHECK_TRUE(data.IsValid());
    // 期望字节布局：header(5) + 子1(9) + 子2(10) + 子3(8) + 子4(9) = 41
    CHECK_EQ(data.size(), 41);

    // 用 Extractor 解析回来，做往返验证
    Extractor::NodeObject parsed(data.data(), data.size());
    CHECK_TRUE(parsed.IsValid());
    CHECK_EQ(parsed.GetTag(), 0x10);
    CHECK_EQ(parsed.GetChildCount(), 4);

    Extractor::NodeUint32 pu32 = parsed.GetChildUint32(0x01);
    CHECK_TRUE(pu32.IsValid());
    CHECK_EQ_HEX(pu32.GetValue(), 0x12345678);

    Extractor::NodeString pstr = parsed.GetChildString(0x03);
    CHECK_TRUE(pstr.IsValid());
    CHECK_STR(pstr.GetString(), "hello");

    Extractor::NodeBinary pbin = parsed.GetChildBinary(0x04);
    CHECK_TRUE(pbin.IsValid());
    CHECK_EQ(pbin.GetLength(), 3);
    CHECK_EQ(pbin.GetValue()[0], 0xAA);
    CHECK_EQ(pbin.GetValue()[1], 0xBB);
    CHECK_EQ(pbin.GetValue()[2], 0xCC);

    pu32 = parsed.GetChildUint32(0x02);
    CHECK_TRUE(pu32.IsValid());
    CHECK_EQ_HEX(pu32.GetValue(), 0xCAFEBABE);
}

// ---------------------------------------------------------------------------
// Test 5: Builder 嵌套组装 + 解析
//   outer(0x20) -> inner(0x21) -> uint32(0x05, 0xDEADBEEF) + string(0x06, "TLV")
// ---------------------------------------------------------------------------
static void TestBuilderNested()
{
    printf("\n--- Test 5: Builder nested serialize ---\n");

    Builder::NodeUint32 innerU32(0x05, 0xDEADBEEF);
    Builder::NodeString  innerStr(0x06, "TLV");
    Builder::NodeObject<4> inner(0x21);
    CHECK_TRUE(inner.AddChild(&innerU32));
    CHECK_TRUE(inner.AddChild(&innerStr));

    Builder::NodeObject<4> outer(0x20);
    CHECK_TRUE(outer.AddChild(&inner));
    CHECK_TRUE(outer.IsValid());

    MemoryBuffer data = outer.Serialize();
    CHECK_TRUE(data.IsValid());
    // outer header(5) + inner(5+9+8=22) = 27
    CHECK_EQ(data.size(), 27);

    Extractor::NodeObject pOuter(data.data(), data.size());
    CHECK_TRUE(pOuter.IsValid());
    CHECK_EQ(pOuter.GetTag(), 0x20);
    CHECK_EQ(pOuter.GetChildCount(), 1);

    Extractor::NodeObject pInner = pOuter.GetChildObject(0x21);
    CHECK_TRUE(pInner.IsValid());
    CHECK_EQ(pInner.GetTag(), 0x21);
    CHECK_EQ(pInner.GetChildCount(), 2);

    Extractor::NodeUint32 v = pInner.GetChildUint32(0x05);
    CHECK_TRUE(v.IsValid());
    CHECK_EQ_HEX(v.GetValue(), 0xDEADBEEF);

    Extractor::NodeString s = pInner.GetChildString(0x06);
    CHECK_TRUE(s.IsValid());
    CHECK_STR(s.GetString(), "TLV");
}

int main()
{
    uint8_t buffer[256];
    size_t objEnd = 0;
    size_t totalLen = BuildSampleBuffer(buffer, &objEnd);

    printf("=== Test: Extractor ===\n\n");
    TestDirectNodeString(buffer, objEnd, totalLen);
    TestAsNodeConvert(buffer, objEnd, totalLen);
    TestGetChildByTag(buffer, totalLen);
    TestNestedObject();

    printf("\n=== Test: Builder ===\n\n");
    TestBuilderBasic();
    TestBuilderNested();

    printf("\n=== Result: %d/%d checks passed, %d failed ===\n",
           g_checks - g_failures, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
