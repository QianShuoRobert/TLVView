/** @file TLVView.h
* @brief TLVView — 轻量零拷贝 TLV (Tag-Length-Value) 编解码库
* @author qianshuo
* @version 1.0
* @date 2026-07-07
* @details
*   TLV 格式： Tag(1字节) + Length(4字节，网络字节序) + Value(N字节)
*   所有长度均为网络字节序 (Big-Endian)
*   Tag 与 Length 的字段类型可配置：修改 namespace TLVView 内的 TagType / LengthType
*   的 using 声明即可，可选 uint8_t / uint16_t / uint32_t。
*   修改后 TAG_LENGTH / LEN_LENGTH / VALUE_MAX_LENGTH 等常量及所有读写逻辑
*   通过模板特化自动适配，无需改动其他代码。
*   需多种 TLV 共存时，复制本文件并改名 namespace 即可（详见 README）。
* @copyright 
*/

#pragma once

#include <cstring>
#include <array>
#include <cstdint>
#include <string>
#include <limits>
#include <new>       // std::nothrow

namespace TLVView {
    /// @brief Tag 与 Length 的字段类型配置。置于命名空间内，便于复制重命名头文件支持多种 TLV。
    /// @details 复制本文件改名为 TLVView2.h 并把 namespace 改为 TLVView2 后，
    ///          TagType 与 TLVView2::TagType 分属不同命名空间，可同程序共存不冲突。
    ///          不使用模板参数指定 Tag/Length 类型：通常一个程序中很少涉及多种 TLV 结构，
    ///          直接修改此 using 定义最简单；使用模板会让所有节点类都带上模板参数，更加繁琐。
    using TagType    = uint8_t;
    using LengthType = uint32_t;

    template <typename T>
    inline uint8_t* BufferWriteValue(uint8_t* buffer, T value);

    template <>
    inline uint8_t* BufferWriteValue<uint8_t>(uint8_t* buffer, uint8_t value) {
        buffer[0] = value;
        return buffer + sizeof(uint8_t);
    }

    template <>
    inline uint8_t* BufferWriteValue<uint16_t>(uint8_t* buffer, uint16_t value) {
        buffer[0] = (value >> 8) & 0xFF;
        buffer[1] = value & 0xFF;
        return buffer + sizeof(uint16_t);
    }

    template <>
    inline uint8_t* BufferWriteValue<uint32_t>(uint8_t* buffer, uint32_t value) {
        buffer[0] = (value >> 24) & 0xFF;
        buffer[1] = (value >> 16) & 0xFF;
        buffer[2] = (value >> 8) & 0xFF;
        buffer[3] = value & 0xFF;
        return buffer + sizeof(uint32_t);
    }

    template <typename T>
    inline const uint8_t* BufferReadValue(const uint8_t* buffer, T& value);

    template <>
    inline const uint8_t* BufferReadValue<uint8_t>(const uint8_t* buffer, uint8_t& value) {
        value = buffer[0];
        return buffer + sizeof(uint8_t);
    }

    template <>
    inline const uint8_t* BufferReadValue<uint16_t>(const uint8_t* buffer, uint16_t& value) {
        value = (uint16_t)buffer[0] << 8 | (uint16_t)buffer[1];
        return buffer + sizeof(uint16_t);
    }

    template <>
    inline const uint8_t* BufferReadValue<uint32_t>(const uint8_t* buffer, uint32_t& value) {
        value = (uint32_t)buffer[0] << 24 |
            (uint32_t)buffer[1] << 16 |
            (uint32_t)buffer[2] << 8 |
            (uint32_t)buffer[3];
        return buffer + sizeof(uint32_t);
    }

    /// @brief 定义全局常量
    const size_t VALUE_MAX_LENGTH = std::numeric_limits<LengthType>::max();
    const size_t TAG_LENGTH = sizeof(TagType);
    const size_t LEN_LENGTH = sizeof(LengthType);

    /**
     * @brief 轻量字符串视图（C++11 兼容，零拷贝）。
     * @details 仅持有指针和长度，不拥有数据，引用的 buffer 须在使用期间保持有效。
     *          Builder 与 Extractor 共用。
     */
    class StringView {
    public:
        StringView() : m_data(nullptr), m_size(0) {}
        StringView(const char* data, size_t size) : m_data(data), m_size(size) {}

        const char* data()  const { return m_data; }
        size_t      size()  const { return m_size; }
        bool        empty() const { return m_size == 0; }

        /// @brief 支持 std::string 隐式构造，便于赋值/比较
        operator std::string() const {
            return std::string(m_data, m_size);
        }

    private:
        const char* m_data;
        size_t      m_size;
    };

    /**
     * @brief 轻量内存缓冲区（C++11 兼容，零异常）。
     * @details 拥有一块通过 operator new(std::nothrow) 分配的内存，析构时释放。
     *          分配失败不抛异常，构造为空对象（IsValid()==false）。
     *          仅支持移动语义，不可拷贝。用于 Builder::Serialize() 的无异常返回值。
     */
    class MemoryBuffer {
    public:
        /// @brief 构造空缓冲区（无效态）
        MemoryBuffer() : m_data(nullptr), m_size(0) {}

        /// @brief 析构释放内存
        ~MemoryBuffer() {
            ::operator delete(m_data);
        }

        /// @brief 移动构造，转移所有权
        MemoryBuffer(MemoryBuffer&& other) noexcept
            : m_data(other.m_data), m_size(other.m_size) {
            other.m_data = nullptr;
            other.m_size = 0;
        }

        /// @brief 移动赋值，转移所有权
        MemoryBuffer& operator=(MemoryBuffer&& other) noexcept {
            if (this != &other) {
                ::operator delete(m_data);
                m_data = other.m_data;
                m_size = other.m_size;
                other.m_data = nullptr;
                other.m_size = 0;
            }
            return *this;
        }

        /// @brief 禁止拷贝
        MemoryBuffer(const MemoryBuffer&) = delete;
        MemoryBuffer& operator=(const MemoryBuffer&) = delete;

        /// @brief 工厂方法：分配 size 字节，失败返回无效态对象（不抛异常）
        static MemoryBuffer Create(size_t size) {
            MemoryBuffer buf;
            if (size == 0)
                return buf;  // 0 字节视为无效，避免分配
            buf.m_data = static_cast<uint8_t*>(::operator new(size, std::nothrow));
            if (buf.m_data)
                buf.m_size = size;
            return buf;
        }

        /// @brief 分配是否成功
        bool        IsValid() const { return m_data != nullptr; }
        /// @brief 数据指针（未分配时为 nullptr）
        uint8_t*    data()        { return m_data; }
        const uint8_t* data() const { return m_data; }
        /// @brief 数据字节数（未分配时为 0）
        size_t      size()  const { return m_size; }

    private:
        uint8_t* m_data;
        size_t   m_size;
    };

    namespace Builder {
    /**
     * @brief Node 基类，不可实例化
     */
    class NodeBase {
    public:
        explicit NodeBase(TagType tag) : m_tag(tag) {}
        virtual ~NodeBase() = default;
        TagType GetTag() const { return m_tag; }

        /// @brief 序列化为 MemoryBuffer（零拷贝写入，内部分配，无异常）
        /// @details 内部用 operator new(std::nothrow) 分配内存，失败时返回
        ///          IsValid()==false 的空对象（不抛异常）。
        ///          调用方须检查返回值的 IsValid()。
        MemoryBuffer Serialize() const {
            size_t totalLen = CalcTotalSize();
            MemoryBuffer buf = MemoryBuffer::Create(totalLen);
            if (buf.IsValid())
                Write(buf.data());
            return buf;
        }

        /**
         * @brief 序列化到外部 buffer（零拷贝写入，无分配）
         * @param buf    目标 buffer，须至少 CalcTotalSize() 字节
         * @param bufLen  buffer 容量
         * @return true 表示成功写入；false 表示 buffer 空间不足
         */
        bool Serialize(uint8_t* buf, size_t bufLen) const {
            size_t totalLen = CalcTotalSize();
            if (bufLen < totalLen)
                return false;
            Write(buf);
            return true;
        }
        virtual size_t   CalcTotalSize() const = 0;
        virtual uint8_t* Write(uint8_t* buf) const = 0;
        virtual bool IsValid() const = 0;
    protected:
        TagType m_tag;
    };

    /**
     * @brief Object类型节点
     * @tparam N 子节点最大数量
     */
    template <size_t N>
    class NodeObject :public NodeBase {
    public:
        // 修改：只接收 tag，稍后通过 AddChild 添加子节点
        explicit NodeObject(uint8_t tag) : NodeBase(tag), m_children{} {}
        bool AddChild(const NodeBase* pChild) {
            for (size_t i = 0; i < N; ++i) {
                if (m_children[i] == nullptr) {
                    m_children[i] = pChild;
                    return true;
                }
            }
            return false;
        }
        bool IsValid() const override {
            for (const auto& c : m_children) {
                if (c && c->IsValid())
                    return true;
            }
            return false;
        }
    private:
        std::array<const NodeBase*, N> m_children{};   // 值初始化为 nullptr
        size_t CalcTotalSize() const override {
            return TAG_LENGTH + LEN_LENGTH + CalcPayloadSize();
        }
        uint8_t* Write(uint8_t* buf) const override {
            size_t payloadSize = CalcPayloadSize();
            // tag
            buf = BufferWriteValue(buf, m_tag);
            // length
            buf = BufferWriteValue(buf, static_cast<uint32_t>(payloadSize));
            // value
            for (const auto& c : m_children) {
                if (c && c->IsValid())
                    buf = c->Write(buf);
            }
            return buf;
        }
        size_t CalcPayloadSize() const {
            size_t len = 0;
            for (const auto& c : m_children) {
                len += (c ? c->CalcTotalSize() : 0);
            }
            return len;
        }
    };

    /**
     * @brief UINT32类型节点
     */
    class NodeUint32 :public NodeBase {
    public:
        NodeUint32(uint8_t tag, uint32_t value) : NodeBase(tag), m_value(value) {}
        bool IsValid() const override { return true; }
    private:
        uint32_t m_value;
        size_t CalcTotalSize() const override {
            return TAG_LENGTH + LEN_LENGTH + sizeof(m_value);
        }
        uint8_t* Write(uint8_t* buf) const override {
            // tag
            buf = BufferWriteValue(buf, m_tag);
            // length
            buf = BufferWriteValue(buf, (uint32_t)sizeof(uint32_t));
            // value
            buf = BufferWriteValue(buf, m_value);
            return buf;
        }
    };

    /**
     * @brief Binary类型节点
     */
    class NodeBinary : public NodeBase {
    public:
        NodeBinary(uint8_t tag, const uint8_t* data, size_t length)
            : NodeBase(tag), m_pData(data), m_szLen(length) {
            if (m_szLen > VALUE_MAX_LENGTH) {
                m_pData = nullptr;  // 无效态
                m_szLen = 0;
            }
        }

        bool IsValid() const override {
            return m_pData && m_szLen;
        }
    private:
        const uint8_t* m_pData;
        size_t           m_szLen;
        size_t CalcTotalSize() const override {
            return TAG_LENGTH + LEN_LENGTH + m_szLen;
        }
        uint8_t* Write(uint8_t* buf) const override {
            // tag
            buf = BufferWriteValue(buf, m_tag);
            // length
            buf = BufferWriteValue(buf, static_cast<uint32_t>(m_szLen));
            // value
            std::memcpy(buf, m_pData, m_szLen);
            buf += m_szLen;
            return buf;
        }
    };

    /**
     * @brief String类型节点
     */
    class NodeString :public NodeBinary {
    public:
        NodeString(uint8_t tag, const char* str)
            : NodeBinary(tag, reinterpret_cast<const uint8_t*>(str),
                str ? std::strlen(str) : 0) {
        }

        /// @brief 从 StringView 构造（零拷贝视图，引用的 buffer 须覆盖 Serialize 调用）
        NodeString(uint8_t tag, const StringView& sv)
            : NodeBinary(tag, reinterpret_cast<const uint8_t*>(sv.data()), sv.size()) {
        }
    };

    } // namespace Builder

    namespace Extractor {
    /// @brief 前向声明，供 NodeBase::AsNodeXxx 使用
    class NodeUint32;
    class NodeBinary;
    class NodeString;
    class NodeObject;

    /// @brief StringView 已提取至 TLVView 命名空间，此处直接复用 StringView

    /**
     * @brief 节点基类 — 内存视图，不拷贝数据。
     * @details m_pValue 指向 TLV 的 Value 起始位置，m_length 是 Value 的长度（从 Length 字段解析）。
     *          tag 从 buffer 中解析。
     */
    class NodeBase {
    public:
        NodeBase() : m_pValue(nullptr), m_length(0), m_tag(0) {}

        /**
         * @brief 从 buffer 解析 tag 和 value length。
         * @details buffer 应指向 TLV 段的起始位置（tag 字节）。
         */
        explicit NodeBase(const uint8_t* buffer, size_t totalLen)
            : m_pValue(nullptr), m_length(0), m_tag(0) {
            if (totalLen < TAG_LENGTH + LEN_LENGTH)
                return;  // 无效 buffer，保持无效态
            buffer = BufferReadValue(buffer, m_tag);            // 读 tag
            buffer = BufferReadValue(buffer, m_length);          // 读 length
            // 校验 m_length 不超出实际剩余字节，防止后续遍历越界读
            if (m_length > totalLen - TAG_LENGTH - LEN_LENGTH) {
                m_pValue = nullptr;  // 无效态，抑制后续访问
                return;
            }
            m_pValue = buffer;
        }

        /// @brief 构造成功后返回 true；若 buffer 数据不完整/格式错误则为 false
        bool        IsValid()   const { return m_pValue != nullptr; }
        TagType     GetTag()    const { return m_tag; }
        const uint8_t* GetValue()  const { return m_pValue; }
        LengthType  GetLength() const { return m_length; }

        /// @brief AsNodeXxx 声明，实现在所有子类定义之后（子类此时才完整）
        NodeUint32 AsNodeUint32() const;
        NodeBinary AsNodeBinary() const;
        NodeString AsNodeString() const;
        NodeObject AsNodeObject() const;

    protected:
        const uint8_t* m_pValue;   ///< 指向 TLV Value 起始位置
        LengthType  m_length;   ///< Value 的长度（从 Length 字段解析）
        TagType     m_tag;      ///< Tag
    };

    /**
     * @brief uint32 值节点 — m_pValue 指向 4 字节的 uint32 网络序数据
     */
    class NodeUint32 : public NodeBase {
    public:
        NodeUint32() : NodeBase(), m_value(0) {}

        explicit NodeUint32(const uint8_t* buffer, size_t totalLen)
            : NodeBase(buffer, totalLen), m_value(0) {
            if (!NodeBase::IsValid() || m_length < sizeof(uint32_t)) {
                m_pValue = nullptr;  // 抑制无效态
                return;
            }
            BufferReadValue(m_pValue, m_value);
        }

        /// @brief 从 NodeBase 视图构造，复用已解析的 tag/length/value 指针
        NodeUint32(const NodeBase& other)
            : NodeBase(other), m_value(0) {
            if (!NodeBase::IsValid() || m_length < sizeof(uint32_t)) {
                m_pValue = nullptr;
                return;
            }
            BufferReadValue(m_pValue, m_value);
        }

        /// @brief 构造成功且 value 长度满足 uint32 要求时返回 true
        bool IsValid() const {
            return NodeBase::IsValid() && m_length >= sizeof(uint32_t);
        }

        uint32_t GetValue() const { return m_value; }

    private:
        uint32_t m_value;
    };

    /**
     * @brief Binary 值节点 — m_pValue 指向原始数据，不拷贝
     */
    class NodeBinary : public NodeBase {
    public:
        NodeBinary() : NodeBase() {}

        explicit NodeBinary(const uint8_t* buffer, size_t totalLen)
            : NodeBase(buffer, totalLen) {}

        /// @brief 从 NodeBase 视图构造
        NodeBinary(const NodeBase& other) : NodeBase(other) {}
    };

    /**
     * @brief String 值节点 — m_pValue 指向字符串数据，不拷贝。
     * @details 提供零拷贝的 StringView 接口和拷贝的 std::string 接口。
     */
    class NodeString : public NodeBase {
    public:
        NodeString() : NodeBase() {}

        explicit NodeString(const uint8_t* buffer, size_t totalLen)
            : NodeBase(buffer, totalLen) {}

        /// @brief 从 NodeBase 视图构造
        NodeString(const NodeBase& other) : NodeBase(other) {}

        /// @brief 零拷贝字符串视图
        StringView GetStringView() const {
            return StringView(reinterpret_cast<const char*>(m_pValue), m_length);
        }

        /// @brief 拷贝出 std::string
        std::string GetString() const {
            return std::string(reinterpret_cast<const char*>(m_pValue), m_length);
        }
    };

    /**
     * @brief Object 节点 — m_pValue 指向子节点序列起始位置，m_length 是子节点序列总字节数。
     * @details 不预先解析子节点，通过 GetChild 按需渐进式解析（可按 tag 匹配，或按序号遍历未知 tag 的子节点）。
     *          GetChildCount 实时遍历 buffer 统计，不缓存。
     */
    class NodeObject : public NodeBase {
    public:
        NodeObject() : NodeBase() {}

        explicit NodeObject(const uint8_t* buffer, size_t totalLen)
            : NodeBase(buffer, totalLen) {}

        /// @brief 从 NodeBase 视图构造
        NodeObject(const NodeBase& other) : NodeBase(other) {}

        /**
         * @brief 实时统计子节点数量（遍历 buffer）
         */
        size_t GetChildCount() const {
            size_t offset = 0;
            size_t count = 0;
            while (NextChildOffset(offset)) {
                ++count;
            }
            return count;
        }

        /**
         * @brief 获取第 index 个子节点（不按 tag 过滤），用于未知 tag 情况下的顺序遍历。
         * @param index 要获取的子节点序号（0 基）
         * @return 解析成功的子节点视图；index 越界或数据不完整时返回无效态（IsValid()==false）
         */
        NodeBase GetChildAt(size_t index) const {
            size_t offset = 0;
            size_t curIndex = 0;
            while (NextChildOffset(offset)) {
                if (curIndex == index)
                    return NodeBase(m_pValue + offset, m_length - offset);
                ++curIndex;
            }
            return NodeBase();  // 无效态
        }

        /**
         * @brief 子节点迭代器 — 按顺序遍历所有子节点（不按 tag 过滤），用于未知 tag 情况下的 range-for 遍历。
         * @details 解引用返回当前子节点的 NodeBase 视图。
         */
        class ChildIterator {
        public:
            ChildIterator() : m_obj(nullptr), m_offset(0) {}
            ChildIterator(const NodeObject* obj, size_t offset) : m_obj(obj), m_offset(offset) {}

            NodeBase operator*() const {
                return NodeBase(m_obj->m_pValue + m_offset, m_obj->m_length - m_offset);
            }

            ChildIterator& operator++() {
                m_obj->NextChildOffset(m_offset);
                return *this;
            }

            bool operator==(const ChildIterator& rhs) const {
                return m_offset == rhs.m_offset;
            }
            bool operator!=(const ChildIterator& rhs) const {
                return m_offset != rhs.m_offset;
            }

        private:
            const NodeObject* m_obj;
            size_t            m_offset;
        };

        /// @brief range-for 起点：定位到第一个子节点（offset=0，若 buffer 非空即有效）
        ChildIterator begin() const {
            return ChildIterator(this, 0);
        }

        /// @brief range-for 终点：offset 超出 m_length 即结束
        ChildIterator end() const {
            return ChildIterator(this, m_length);
        }

        /**
         * @brief 获取 uint32 子节点。
         * @param tag   要匹配的 tag
         * @param index 当存在多个相同 tag 时，指定获取第几个（0 基）
         * @return 解析成功的子节点；未找到或 value 长度不足时返回无效态（IsValid()==false）
         */
        NodeUint32 GetChildUint32(TagType tag, size_t index = 0) const {
            return FindChild(tag, index).AsNodeUint32();
        }

        /**
         * @brief 获取 Binary 子节点。
         */
        NodeBinary GetChildBinary(TagType tag, size_t index = 0) const {
            return FindChild(tag, index).AsNodeBinary();
        }

        /**
         * @brief 获取 String 子节点。
         */
        NodeString GetChildString(TagType tag, size_t index = 0) const {
            return FindChild(tag, index).AsNodeString();
        }

        /**
         * @brief 获取 Object 子节点。
         */
        NodeObject GetChildObject(TagType tag, size_t index = 0) const {
            return FindChild(tag, index).AsNodeObject();
        }

    private:
        /**
         * @brief 从当前 offset 推进到下一个子节点起始位置。
         * @details 成功推进返回 true；到末尾或数据不完整返回 false。
         */
        bool NextChildOffset(size_t& offset) const {
            if (offset >= m_length)
                return false;

            size_t remaining = m_length - offset;
            if (remaining < TAG_LENGTH + LEN_LENGTH)
                return false;

            uint8_t const* p = m_pValue + offset + TAG_LENGTH;
            LengthType childLen = 0;
            BufferReadValue(p, childLen);

            if (remaining < TAG_LENGTH + LEN_LENGTH + childLen)
                return false;

            offset += TAG_LENGTH + LEN_LENGTH + childLen;
            return true;
        }

        /**
         * @brief 按 tag 匹配子节点，index 指定第几个匹配项（0 基）。
         * @details 仅定位子节点起始位置并构造为 NodeBase（解析 tag + length），
         *          不做类型转换，由调用方通过 AsNodeXxx 转换为具体类型。
         *          未找到时返回无效态的 NodeBase（IsValid()==false）。
         */
        NodeBase FindChild(TagType tag, size_t index) const {
            size_t offset = 0;
            size_t matchCount = 0;

            while (offset < m_length) {
                size_t remaining = m_length - offset;
                if (remaining < TAG_LENGTH + LEN_LENGTH)
                    break;

                uint8_t const* p = m_pValue + offset;
                TagType childTag = 0;
                p = BufferReadValue(p, childTag);
                LengthType childLen = 0;
                p = BufferReadValue(p, childLen);

                if (remaining < TAG_LENGTH + LEN_LENGTH + childLen)
                    break;

                if (childTag == tag) {
                    if (matchCount == index) {
                        size_t nodeTotalLen = TAG_LENGTH + LEN_LENGTH + childLen;
                        return NodeBase(m_pValue + offset, nodeTotalLen);
                    }
                    ++matchCount;
                }

                offset += TAG_LENGTH + LEN_LENGTH + childLen;
            }
            return NodeBase();  // 无效态
        }
    };

    /// @brief AsNodeXxx 实现（此时所有子类均已完整定义）

    inline NodeUint32 NodeBase::AsNodeUint32() const {
        return NodeUint32(*this);
    }

    inline NodeBinary NodeBase::AsNodeBinary() const {
        return NodeBinary(*this);
    }

    inline NodeString NodeBase::AsNodeString() const {
        return NodeString(*this);
    }

    inline NodeObject NodeBase::AsNodeObject() const {
        return NodeObject(*this);
    }

    } // namespace Extractor

} // namespace TLVView
