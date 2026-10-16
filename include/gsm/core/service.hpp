#pragma once
#include "gsm/core/at_engine.hpp"
#include "gsm/core/at_types.hpp"

namespace gsm
{

enum class Charset : uint8_t
{
    Unknown = 0,
    Gsm,
    Ucs2
};

struct SessionState
{
    bool    text_mode     = false;
    Charset charset       = Charset::Unknown;
    bool    csmp_ucs2     = false;
    bool    storage_ready = false;
    bool    echo_off      = false;
    void    invalidate() { *this = SessionState{}; }
};

class IService : public IAtObserver
{
  public:
    virtual bool            active() const    = 0;
    virtual void            pump()            = 0;
    virtual void            cancel()          = 0;
    virtual const char*     step_name() const = 0;
    virtual const AtResult& result() const    = 0;
};

} // namespace gsm
