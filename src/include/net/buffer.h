#pragma once
#include <vector>
#include <cstring>
#include <cstdint>
#include <stdexcept>

namespace mprpc {

class Buffer {
public:
    explicit Buffer(size_t initial_capacity = 4096) 
        : m_data(initial_capacity), m_read_pos(0), m_write_pos(0) {}

    size_t ReadableBytes() const { return m_write_pos - m_read_pos; }
    size_t WritableBytes() const { return m_data.size() - m_write_pos; }

    const char* Peek() const { return m_data.data() + m_read_pos; }
    
    void Retrieve(size_t len) {
        if (len > ReadableBytes()) {
            throw std::runtime_error("Retrieve beyond readable bytes");
        }
        m_read_pos += len;
        if (m_read_pos == m_write_pos) {
            m_read_pos = m_write_pos = 0;
        }
    }

    std::string RetrieveAsString(size_t len) {
        if (len > ReadableBytes()) {
            throw std::runtime_error("RetrieveAsString beyond readable bytes");
        }
        std::string result(Peek(), len);
        Retrieve(len);
        return result;
    }

    std::string RetrieveAllAsString() {
        return RetrieveAsString(ReadableBytes());
    }

    void Append(const char* data, size_t len) {
        EnsureWritable(len);
        std::memcpy(m_data.data() + m_write_pos, data, len);
        m_write_pos += len;
    }

    void Append(const std::string& str) {
        Append(str.data(), str.size());
    }

    void AppendUint32(uint32_t value) {
        Append(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    uint32_t ReadUint32() {
        if (ReadableBytes() < sizeof(uint32_t)) {
            throw std::runtime_error("Not enough bytes for uint32");
        }
        uint32_t value;
        std::memcpy(&value, Peek(), sizeof(value));
        Retrieve(sizeof(value));
        return value;
    }

    void EnsureWritable(size_t len) {
        if (WritableBytes() < len) {
            size_t new_size = m_data.size() * 2;
            while (new_size < m_write_pos + len) {
                new_size *= 2;
            }
            m_data.resize(new_size);
        }
    }

    void Clear() {
        m_read_pos = m_write_pos = 0;
    }

    char* WriteBegin() { return m_data.data() + m_write_pos; }

private:
    std::vector<char> m_data;
    size_t m_read_pos;
    size_t m_write_pos;
};

} // namespace mprpc
