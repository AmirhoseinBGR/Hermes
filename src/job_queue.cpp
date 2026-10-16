#include "gsm/queue/job_queue.hpp"

namespace gsm
{

const char* to_string(JobKind k)
{
    switch (k)
    {
    case JobKind::None:
        return "none";
    case JobKind::SmsSend:
        return "sms-send";
    case JobKind::SmsRead:
        return "sms-read";
    case JobKind::SmsScan:
        return "sms-scan";
    case JobKind::SmsDeleteIndex:
        return "sms-del";
    case JobKind::SmsDeleteRead:
        return "sms-del-read";
    case JobKind::CallDial:
        return "dial";
    case JobKind::CallAnswer:
        return "answer";
    case JobKind::CallHangup:
        return "hangup";
    case JobKind::StatusSweep:
        return "status";
    case JobKind::UssdQuery:
        return "ussd";
    }
    return "?";
}

const char* to_string(JobState s)
{
    switch (s)
    {
    case JobState::Queued:
        return "queued";
    case JobState::Running:
        return "running";
    case JobState::Succeeded:
        return "ok";
    case JobState::Failed:
        return "failed";
    case JobState::Cancelled:
        return "cancelled";
    }
    return "?";
}

bool JobQueue::push(const Job& job, JobCallback cb)
{
    if (full()) return false;
    if (job.urgent)
    {
        head_             = static_cast<uint8_t>((head_ + GSM_QUEUE_DEPTH - 1) % GSM_QUEUE_DEPTH);
        slots_[head_].job = job;
        slots_[head_].cb  = cb;
    }
    else
    {
        const uint8_t tail = static_cast<uint8_t>((head_ + count_) % GSM_QUEUE_DEPTH);
        slots_[tail].job   = job;
        slots_[tail].cb    = cb;
    }
    ++count_;
    return true;
}

bool JobQueue::pop(Job& out, JobCallback& cb)
{
    if (!count_ || paused_) return false;
    out = slots_[head_].job;
    cb  = slots_[head_].cb;
    slots_[head_].job.clear();
    slots_[head_].cb.clear();
    head_ = static_cast<uint8_t>((head_ + 1) % GSM_QUEUE_DEPTH);
    --count_;
    return true;
}

const Job* JobQueue::at(uint8_t i) const
{
    if (i >= count_) return nullptr;
    return &slots_[(head_ + i) % GSM_QUEUE_DEPTH].job;
}

bool JobQueue::cancel(JobId id, Job& out, JobCallback& cb)
{
    for (uint8_t i = 0; i < count_; ++i)
    {
        const uint8_t idx = static_cast<uint8_t>((head_ + i) % GSM_QUEUE_DEPTH);
        if (slots_[idx].job.id != id) continue;
        out       = slots_[idx].job;
        out.state = JobState::Cancelled;
        cb        = slots_[idx].cb;
        for (uint8_t j = i; j + 1 < count_; ++j)
        {
            const uint8_t a = static_cast<uint8_t>((head_ + j) % GSM_QUEUE_DEPTH);
            const uint8_t b = static_cast<uint8_t>((head_ + j + 1) % GSM_QUEUE_DEPTH);
            slots_[a]       = slots_[b];
        }
        --count_;
        return true;
    }
    return false;
}

void JobQueue::clear()
{
    cancelled_count_ = 0;
    while (count_ && cancelled_count_ < GSM_QUEUE_DEPTH)
    {
        Slot& src                      = slots_[head_];
        src.job.state                  = JobState::Cancelled;
        cancelled_[cancelled_count_++] = src;
        src.job.clear();
        src.cb.clear();
        head_ = static_cast<uint8_t>((head_ + 1) % GSM_QUEUE_DEPTH);
        --count_;
    }
    count_ = 0;
}

bool JobQueue::take_cancelled(Job& out, JobCallback& cb)
{
    if (!cancelled_count_) return false;
    --cancelled_count_;
    out = cancelled_[cancelled_count_].job;
    cb  = cancelled_[cancelled_count_].cb;
    return true;
}

uint8_t JobQueue::purge_stale(uint32_t now, uint32_t max_age_ms)
{
    if (!count_ || !max_age_ms) return 0;
    uint8_t purged = 0;
    uint8_t i      = 0;
    while (i < count_)
    {
        const uint8_t idx = static_cast<uint8_t>((head_ + i) % GSM_QUEUE_DEPTH);
        if (static_cast<uint32_t>(now - slots_[idx].job.enqueued_ms) > max_age_ms)
        {
            if (cancelled_count_ < GSM_QUEUE_DEPTH)
            {
                Slot dropped                   = slots_[idx];
                dropped.job.state              = JobState::Cancelled;
                cancelled_[cancelled_count_++] = dropped;
            }
            for (uint8_t j = i; j + 1 < count_; ++j)
            {
                const uint8_t a = static_cast<uint8_t>((head_ + j) % GSM_QUEUE_DEPTH);
                const uint8_t b = static_cast<uint8_t>((head_ + j + 1) % GSM_QUEUE_DEPTH);
                slots_[a]       = slots_[b];
            }
            --count_;
            ++purged;
        }
        else
        {
            ++i;
        }
    }
    return purged;
}

} // namespace gsm
