#pragma once
#include "gsm/codec/phone_number.hpp"
#include "gsm/gsm_config.hpp"
#include "gsm/util/static_string.hpp"

namespace gsm
{

enum class SmsEncoding : uint8_t
{
    Auto = 0,
    Gsm7,
    Ucs2
};

struct SmsMessage
{
    StaticString<GSM_SMS_NUMBER_BYTES> number;
    StaticString<GSM_SMS_TEXT_BYTES>   text;
    StaticString<24>                   timestamp;
    int16_t                            storage_index = -1;
    SmsEncoding                        encoding      = SmsEncoding::Auto;
    bool                               unread        = true;
    void                               clear()
    {
        number.clear();
        text.clear();
        timestamp.clear();
        storage_index = -1;
        encoding      = SmsEncoding::Auto;
        unread        = true;
    }
};

struct SmsSendReport
{
    int16_t  reference = -1;
    AtResult result;
};

template <uint8_t N> class SmsInbox
{
  public:
    SmsMessage& acquire()
    {
        SmsMessage& slot = slots_[head_];
        slot.clear();
        head_ = static_cast<uint8_t>((head_ + 1) % N);
        if (count_ < N) ++count_;
        return slot;
    }
    uint8_t                  count() const { return count_; }
    static constexpr uint8_t capacity() { return N; }
    const SmsMessage&        at(uint8_t i) const
    {
        const uint8_t idx = static_cast<uint8_t>((head_ + N - 1 - (i % N)) % N);
        return slots_[idx];
    }
    void clear()
    {
        head_  = 0;
        count_ = 0;
    }

  private:
    SmsMessage slots_[N];
    uint8_t    head_  = 0;
    uint8_t    count_ = 0;
};

} // namespace gsm
