#pragma once
#include "gsm/modem/modem.hpp"

namespace gsm::tools
{

class SerialBridge final : public IAtObserver
{
  public:
    SerialBridge(hal::IUart& console, Modem& modem) : console_(console), modem_(modem) {}

    void tick() { passthrough_ ? pump_passthrough() : pump_managed(); }

    bool passthrough() const { return passthrough_; }
    void set_passthrough(bool on)
    {
        passthrough_ = on;
        line_.clear();
    }

    void on_line(const char* line, uint16_t len) override { echo(line, len); }
    void on_final(const AtResult& res) override
    {
        echo(to_string(res.status), 0);
        busy_ = false;
    }

  private:
    void echo(const char* text, uint16_t len)
    {
        if (!text) return;
        const uint16_t n = len ? len : static_cast<uint16_t>(std::strlen(text));
        console_.write(reinterpret_cast<const uint8_t*>(text), n);
        const uint8_t crlf[2] = {'\r', '\n'};
        console_.write(crlf, 2);
    }

    void pump_managed()
    {
        uint8_t b = 0;
        while (console_.read(&b, 1) == 1)
        {
            if (b == '\n' || b == '\r')
            {
                if (line_.empty()) continue;
                if (busy_ || !modem_.at().idle()) { echo("BUSY (engine)", 0); }
                else
                {
                    busy_ = modem_.at().execute(line_.c_str(), 10000, this);
                }
                line_.clear();
                continue;
            }
            if (b == 0x1A)
            {
                modem_.at().write_ctrl_z();
                continue;
            }
            if (b == 0x1B)
            {
                modem_.at().write_escape();
                continue;
            }
            line_.push_back(static_cast<char>(b));
        }
    }

    void pump_passthrough()
    {
        uint8_t  buf[32];
        uint16_t n = console_.read(buf, sizeof(buf));
        if (n) modem_.at().uart().write(buf, n);
        n = modem_.at().uart().read(buf, sizeof(buf));
        if (n) console_.write(buf, n);
    }

    hal::IUart&               console_;
    Modem&                    modem_;
    StaticString<GSM_CMD_MAX> line_;
    bool                      passthrough_ = false;
    bool                      busy_        = false;
};

} // namespace gsm::tools
