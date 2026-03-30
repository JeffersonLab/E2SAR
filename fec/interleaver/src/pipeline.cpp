#include "fec/pipeline.h"

namespace fec {

StageBuffer::StageBuffer(std::span<uint8_t> buf) noexcept
    : buf_(buf)
{}

std::span<uint8_t> StageBuffer::data() noexcept {
    return buf_;
}

std::span<const uint8_t> StageBuffer::data() const noexcept {
    return buf_;
}

void StageBuffer::mark_ready() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        ready_ = true;
    }
    cv_.notify_all();
}

void StageBuffer::wait_ready() {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait(lk, [this]{ return ready_; });
}

bool StageBuffer::is_ready() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return ready_;
}

void StageBuffer::reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    ready_ = false;
}

} // namespace fec
