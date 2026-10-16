#pragma once
#include <cstdint>

namespace gsm
{

template <uint16_t N> class RingBuffer
{
  public:
    bool push(uint8_t b)
    {
        const uint16_t next = advance(head_);
        if (next == tail_)
        {
            overflow_ = true;
            return false;
        }
        buf_[head_] = b;
        head_       = next;
        return true;
    }
    uint16_t push(const uint8_t* d, uint16_t n)
    {
        uint16_t i = 0;
        while (i < n && push(d[i]))
            ++i;
        return i;
    }
    bool pop(uint8_t& out)
    {
        if (tail_ == head_) return false;
        out   = buf_[tail_];
        tail_ = advance(tail_);
        return true;
    }
    uint16_t size() const
    {
        const uint16_t h = head_, t = tail_;
        return h >= t ? static_cast<uint16_t>(h - t) : static_cast<uint16_t>(N - t + h);
    }
    bool empty() const { return head_ == tail_; }
    void clear()
    {
        tail_     = head_;
        overflow_ = false;
    }
    bool overflowed() const { return overflow_; }
    void clear_overflow() { overflow_ = false; }

  private:
    static uint16_t   advance(uint16_t i) { return static_cast<uint16_t>((i + 1) % N); }
    uint8_t           buf_[N]{};
    volatile uint16_t head_     = 0;
    volatile uint16_t tail_     = 0;
    volatile bool     overflow_ = false;
};

} // namespace gsm
