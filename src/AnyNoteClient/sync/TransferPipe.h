#pragma once
#include "SyncClient.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <streambuf>
#include <array>
#include <algorithm>

namespace anynote::sync {
// A bounded in-memory pipe keeps raw HTTP JSON off disk and avoids a full
// Base64 document allocation. Producer and consumer errors wake both sides.
class TransferPipe {
public:
    explicit TransferPipe(TransferControl& control) : m_control(control) {}
    void Close() { std::lock_guard lock(m_mutex); m_closed = true; m_ready.notify_all(); }
    void Abort() { std::lock_guard lock(m_mutex); m_aborted = true; m_ready.notify_all(); }
    size_t Write(const char* data, size_t size) {
        size_t written = 0;
        while (written < size) {
            std::unique_lock lock(m_mutex);
            while (m_chunks.size() >= 4 && !Stopped()) m_ready.wait_for(lock, std::chrono::milliseconds(100));
            if (Stopped()) throw std::runtime_error("TRANSFER_ABORTED");
            auto count = std::min(size - written, size_t{65536});
            m_chunks.emplace_back(data + written, count); written += count; m_ready.notify_all();
        }
        return written;
    }
    size_t Read(char* data, size_t size) {
        std::unique_lock lock(m_mutex);
        while (m_chunks.empty() && !m_closed && !Stopped()) m_ready.wait_for(lock, std::chrono::milliseconds(100));
        if (Stopped()) throw std::runtime_error("TRANSFER_ABORTED");
        if (m_chunks.empty()) return 0;
        auto count = std::min(size, m_chunks.front().size() - m_offset);
        std::copy_n(m_chunks.front().data() + m_offset, count, data); m_offset += count;
        if (m_offset == m_chunks.front().size()) { m_chunks.pop_front(); m_offset = 0; }
        m_ready.notify_all(); return count;
    }
private:
    bool Stopped() const { return m_aborted || m_control.cancelled.load(); }
    TransferControl& m_control;
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<std::string> m_chunks;
    size_t m_offset = 0;
    bool m_closed = false, m_aborted = false;
};
class PipeOutput : public std::streambuf {
public:
    explicit PipeOutput(TransferPipe& pipe) : m_pipe(pipe) {}
    std::streamsize xsputn(const char* p, std::streamsize n) override { return static_cast<std::streamsize>(m_pipe.Write(p, static_cast<size_t>(n))); }
    int_type overflow(int_type c) override { if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c); char b = traits_type::to_char_type(c); m_pipe.Write(&b, 1); return c; }
private: TransferPipe& m_pipe;
};
class PipeInput : public std::streambuf {
public:
    explicit PipeInput(TransferPipe& pipe) : m_pipe(pipe) {}
    int_type underflow() override {
        auto size = m_pipe.Read(m_buffer.data(), m_buffer.size());
        if (!size) return traits_type::eof();
        setg(m_buffer.data(), m_buffer.data(), m_buffer.data() + size); return traits_type::to_int_type(*gptr());
    }
private: TransferPipe& m_pipe; std::array<char, 65536> m_buffer{};
};
}
