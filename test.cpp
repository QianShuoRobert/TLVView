#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include "TLVView.h"

using namespace TLVView;

int main()
{
    uint8_t buffer[256];
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

    size_t objEnd = offset;

    // 单独节点5（在 object 之后）：tag=0x04, string "world"
    const char* str2 = "world";
    size_t strLen2 = strlen(str2);
    buffer[offset++] = 0x04;
    offset = BufferWriteValue(buffer + offset, (uint32_t)strLen2) - buffer;
    memcpy(buffer + offset, str2, strLen2);
    offset += strLen2;

    size_t totalLen = offset;

    printf("=== Test: Extractor ===\n\n");

    // --- NodeString 直接解析 ---
    {
        printf("--- Test 1: Direct NodeString ---\n");
        Extractor::NodeString node5(buffer + objEnd, totalLen - objEnd);
        StringView sv = node5.GetStringView();
        printf("  tag=0x%02X, view=%.*s\n", node5.GetTag(), (int)sv.size(), sv.data());
        std::string s = node5.GetString();
        printf("  tag=0x%02X, std::string=%s\n", node5.GetTag(), s.c_str());
    }

    // --- NodeBase AsNodeXxx 转换 ---
    {
        printf("\n--- Test 1b: NodeBase AsNodeXxx ---\n");
        // 以 NodeBase 视图解析，再转成具体类型
        Extractor::NodeBase base(buffer + objEnd, totalLen - objEnd);
        printf("  base tag=0x%02X, length=%u\n", base.GetTag(), (unsigned)base.GetLength());

        // 转成 NodeString
        Extractor::NodeString strNode = base.AsNodeString();
        printf("  as string: %s\n", strNode.GetString().c_str());

        // object 的第一个子节点用 NodeBase 取，再 AsNodeUint32
        Extractor::NodeObject obj(buffer, totalLen);
        Extractor::NodeBase childBase;
        // 用 GetChild(NodeUint32&) 间接验证 AsNode 链路
        Extractor::NodeUint32 u32;
        if (obj.GetChild(u32, 0x01))
            printf("  via GetChild: tag=0x01 value=0x%08X\n", u32.GetValue());
    }

    // --- NodeObject + GetChild(out, tag) ---
    {
        printf("\n--- Test 2: NodeObject with GetChild(out, tag) ---\n");
        Extractor::NodeObject obj(buffer, totalLen);
        printf("  child count: %u\n", (unsigned)obj.GetChildCount());

        Extractor::NodeUint32 u32;
        if (obj.GetChild(u32, 0x01))
            printf("  tag=0x01: value=0x%08X\n", u32.GetValue());

        if (obj.GetChild(u32, 0x02))
            printf("  tag=0x02 (first): value=0x%08X\n", u32.GetValue());

        // 重复 tag，取第2个
        if (obj.GetChild(u32, 0x02, 1))
            printf("  tag=0x02 (second): value=0x%08X\n", u32.GetValue());

        // 取第3个（不存在）
        bool ok = obj.GetChild(u32, 0x02, 2);
        printf("  tag=0x02 (third): %s\n", ok ? "found (FAIL)" : "not found (correct)");

        Extractor::NodeString strChild;
        if (obj.GetChild(strChild, 0x03))
            printf("  tag=0x03: string=%s\n", strChild.GetString().c_str());

        // 不存在的 tag
        Extractor::NodeUint32 notFound;
        printf("  tag=0xFF: %s\n", obj.GetChild(notFound, 0xFF) ? "found (FAIL)" : "not found (correct)");

        // range-for 遍历所有子节点（未知 tag 顺序遍历）
        printf("  range-for children:\n");
        size_t idx = 0;
        for (Extractor::NodeBase child : obj)
        {
            printf("    [%u] tag=0x%02X, length=%u\n",
                   (unsigned)idx, child.GetTag(), (unsigned)child.GetLength());
            ++idx;
        }
    }

    // --- 嵌套 Object 测试 ---
    {
        printf("\n--- Test 3: Nested NodeObject ---\n");
        // 构造：outer(tag=0x20) -> inner(tag=0x21) -> uint32(tag=0x05, value=0xDEADBEEF)
        uint8_t nested[64];
        size_t noff = 0;

        // 最内层 uint32
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
        printf("  outer tag=0x%02X, child count=%u\n", outer.GetTag(), (unsigned)outer.GetChildCount());

        Extractor::NodeObject inner;
        if (outer.GetChild(inner, 0x21))
        {
            printf("  inner tag=0x%02X, child count=%u\n", inner.GetTag(), (unsigned)inner.GetChildCount());

            Extractor::NodeUint32 val;
            if (inner.GetChild(val, 0x05))
                printf("  inner tag=0x05: value=0x%08X\n", val.GetValue());
        }
    }

    // --- Builder 组装测试 ---
    {
        printf("\n=== Test: Builder ===\n\n");

        printf("--- Test 4: Builder basic serialize ---\n");
        // 栈上构造各子节点，由 Object 持有指针（须覆盖 Serialize 调用）
        Builder::NodeUint32 c1(0x01, 0x12345678);
        // 用 StringView 构造 NodeString（验证 TLVView::StringView 重载）
        StringView sv("hello", 5);
        Builder::NodeString c2(0x03, sv);
        const uint8_t bin[] = { 0xAA, 0xBB, 0xCC };
        Builder::NodeBinary c3(0x04, bin, sizeof(bin));
        Builder::NodeUint32 c4(0x02, 0xCAFEBABE);

        Builder::NodeObject<8> obj(0x10);
        obj.AddChild(&c1);
        obj.AddChild(&c2);
        obj.AddChild(&c3);
        obj.AddChild(&c4);

        std::vector<uint8_t> data = obj.Serialize();
        printf("  IsValid=%d, serialized %u bytes:", obj.IsValid(), (unsigned)data.size());
        for (uint8_t b : data) printf(" %02X", b);
        printf("\n");

        // 用 Extractor 解析回来，做往返验证
        Extractor::NodeObject parsed(data.data(), data.size());
        printf("  parsed tag=0x%02X, child count=%u\n",
               parsed.GetTag(), (unsigned)parsed.GetChildCount());

        Extractor::NodeUint32 pu32;
        if (parsed.GetChild(pu32, 0x01))
            printf("  tag=0x01: 0x%08X (expect 0x12345678) %s\n",
                   pu32.GetValue(), pu32.GetValue() == 0x12345678 ? "OK" : "FAIL");

        Extractor::NodeString pstr;
        if (parsed.GetChild(pstr, 0x03))
            printf("  tag=0x03: \"%s\" (expect \"hello\") %s\n",
                   pstr.GetString().c_str(),
                   pstr.GetString() == "hello" ? "OK" : "FAIL");

        Extractor::NodeBinary pbin;
        if (parsed.GetChild(pbin, 0x04))
        {
            printf("  tag=0x04: len=%u, bytes:", (unsigned)pbin.GetDataLength());
            for (LengthType i = 0; i < pbin.GetDataLength(); ++i)
                printf(" %02X", pbin.GetData()[i]);
            printf(" (expect AA BB CC) %s\n",
                   pbin.GetDataLength() == 3 &&
                   pbin.GetData()[0] == 0xAA &&
                   pbin.GetData()[1] == 0xBB &&
                   pbin.GetData()[2] == 0xCC ? "OK" : "FAIL");
        }

        if (parsed.GetChild(pu32, 0x02))
            printf("  tag=0x02: 0x%08X (expect 0xCAFEBABE) %s\n",
                   pu32.GetValue(), pu32.GetValue() == 0xCAFEBABE ? "OK" : "FAIL");
    }

    // --- Builder 嵌套组装测试 ---
    {
        printf("\n--- Test 5: Builder nested serialize ---\n");
        // outer(0x20) -> inner(0x21) -> uint32(0x05, 0xDEADBEEF) + string(0x06, "TLV")
        Builder::NodeUint32 innerU32(0x05, 0xDEADBEEF);
        Builder::NodeString  innerStr(0x06, "TLV");
        Builder::NodeObject<4> inner(0x21);
        inner.AddChild(&innerU32);
        inner.AddChild(&innerStr);

        Builder::NodeObject<4> outer(0x20);
        outer.AddChild(&inner);

        std::vector<uint8_t> data = outer.Serialize();
        printf("  serialized %u bytes:", (unsigned)data.size());
        for (uint8_t b : data) printf(" %02X", b);
        printf("\n");

        // 解析嵌套结构
        Extractor::NodeObject pOuter(data.data(), data.size());
        printf("  outer tag=0x%02X, child count=%u\n",
               pOuter.GetTag(), (unsigned)pOuter.GetChildCount());

        Extractor::NodeObject pInner;
        if (pOuter.GetChild(pInner, 0x21))
        {
            printf("  inner tag=0x%02X, child count=%u\n",
                   pInner.GetTag(), (unsigned)pInner.GetChildCount());

            Extractor::NodeUint32 v;
            if (pInner.GetChild(v, 0x05))
                printf("  tag=0x05: 0x%08X (expect 0xDEADBEEF) %s\n",
                       v.GetValue(), v.GetValue() == 0xDEADBEEF ? "OK" : "FAIL");

            Extractor::NodeString s;
            if (pInner.GetChild(s, 0x06))
                printf("  tag=0x06: \"%s\" (expect \"TLV\") %s\n",
                       s.GetString().c_str(),
                       s.GetString() == "TLV" ? "OK" : "FAIL");
        }
    }

    printf("\n=== All passed ===\n");
    return 0;
}
