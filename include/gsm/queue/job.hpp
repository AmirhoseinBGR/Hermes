#pragma once
#include "gsm/core/at_types.hpp"
#include "gsm/gsm_config.hpp"
#include "gsm/sms/sms_types.hpp"
#include "gsm/ussd/ussd_service.hpp"
#include "gsm/util/delegate.hpp"
#include "gsm/util/static_string.hpp"

namespace gsm
{

using JobId                 = uint16_t;
constexpr JobId kInvalidJob = 0;

enum class JobKind : uint8_t
{
    None = 0,
    SmsSend,
    SmsRead,
    SmsScan,
    SmsDeleteIndex,
    SmsDeleteRead,
    CallDial,
    CallAnswer,
    CallHangup,
    StatusSweep,
    UssdQuery
};

enum class JobState : uint8_t
{
    Queued = 0,
    Running,
    Succeeded,
    Failed,
    Cancelled
};

const char* to_string(JobKind k);
const char* to_string(JobState s);

struct Job
{
    JobId    id     = kInvalidJob;
    JobKind  kind   = JobKind::None;
    JobState state  = JobState::Queued;
    bool     urgent = false;
    // Set when a call interrupted the job: the retry must not consume attempts.
    bool                                  deferred     = false;
    uint8_t                               attempts     = 0;
    uint8_t                               max_attempts = 1;
    int16_t                               index        = -1;
    SmsEncoding                           encoding     = SmsEncoding::Auto;
    StaticString<GSM_SMS_NUMBER_BYTES>    number;
    StaticString<GSM_SMS_TEXT_BYTES>      text;
    StaticString<GSM_USSD_RESPONSE_BYTES> ussd_response;
    AtResult                              result;
    int16_t                               reference   = -1;
    uint32_t                              enqueued_ms = 0;
    void                                  clear() { *this = Job{}; }
};

using JobCallback = Delegate<const Job&>;

} // namespace gsm
