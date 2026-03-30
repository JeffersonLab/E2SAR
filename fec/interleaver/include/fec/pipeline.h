#pragma once
#include <span>
#include <cstdint>
#include <mutex>
#include <condition_variable>

namespace fec {

// StageBuffer — a span of memory with a ready-flag for pipeline synchronization.
//
// Intended use: one producer thread fills the buffer and calls mark_ready().
// One or more consumer threads call wait_ready() to block until the data is available.
// Call reset() before reusing the buffer for the next pipeline cycle.
//
// Thread safety: mark_ready(), wait_ready(), is_ready(), and reset() are all
// safe to call concurrently from different threads.
//
// Typical 3-stage pipeline (double-buffered for throughput):
//
//   StageBuffer cw_buf_A(alloc_A);   // interleave → RS
//   StageBuffer cw_buf_B(alloc_B);   // RS → deinterleave
//
//   Thread 1 (interleaver):
//       interleave(src, cw_buf_A.data(), params);
//       cw_buf_A.mark_ready();
//
//   Thread 2 (RS encoder):
//       cw_buf_A.wait_ready();
//       rs_encode(cw_buf_A.data(), params);
//       cw_buf_B.mark_ready();
//
//   Thread 3 (deinterleaver):
//       cw_buf_B.wait_ready();
//       deinterleave(cw_buf_B.data(), dst, params);
class StageBuffer {
public:
    // Constructs a StageBuffer wrapping an externally-owned byte array.
    // The buffer memory must outlive this object.
    explicit StageBuffer(std::span<uint8_t> buf) noexcept;

    // Non-copyable, non-movable (holds mutex).
    StageBuffer(const StageBuffer&)            = delete;
    StageBuffer& operator=(const StageBuffer&) = delete;

    // Read/write access to the underlying buffer.
    std::span<uint8_t>       data()       noexcept;
    std::span<const uint8_t> data() const noexcept;

    // Producer: signal that the buffer has been fully written.
    // Must be called exactly once per pipeline cycle, after writing is complete.
    void mark_ready();

    // Consumer: block until mark_ready() has been called for this cycle.
    // Returns immediately if the buffer is already ready.
    void wait_ready();

    // Returns true if mark_ready() has been called for the current cycle
    // without blocking.
    bool is_ready() const;

    // Reset the ready flag for the next pipeline cycle.
    // Must only be called when no thread is blocked in wait_ready().
    void reset();

private:
    std::span<uint8_t>      buf_;
    mutable std::mutex      mtx_;
    std::condition_variable cv_;
    bool                    ready_ = false;
};

} // namespace fec
