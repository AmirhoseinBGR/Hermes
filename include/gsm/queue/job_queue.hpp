#pragma once
#include "gsm/queue/job.hpp"

namespace gsm
{

class JobQueue
{
  public:
    bool       push(const Job& job, JobCallback cb);
    bool       pop(Job& out, JobCallback& cb);
    const Job* peek() const { return count_ ? &slots_[head_].job : nullptr; }
    bool       cancel(JobId id, Job& out, JobCallback& cb);
    void       clear();
    bool       take_cancelled(Job& out, JobCallback& cb);

    // Drop jobs queued longer than max_age_ms (prevents accumulation/stuck queue)
    uint8_t purge_stale(uint32_t now, uint32_t max_age_ms);

    uint8_t                  size() const { return count_; }
    bool                     empty() const { return count_ == 0; }
    bool                     full() const { return count_ >= GSM_QUEUE_DEPTH; }
    static constexpr uint8_t capacity() { return GSM_QUEUE_DEPTH; }
    const Job*               at(uint8_t i) const;

    void pause() { paused_ = true; }
    void resume() { paused_ = false; }
    bool paused() const { return paused_; }

  private:
    struct Slot
    {
        Job         job;
        JobCallback cb;
    };
    Slot    slots_[GSM_QUEUE_DEPTH];
    Slot    cancelled_[GSM_QUEUE_DEPTH];
    uint8_t cancelled_count_ = 0;
    uint8_t head_            = 0;
    uint8_t count_           = 0;
    bool    paused_          = false;
};

} // namespace gsm
