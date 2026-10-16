// Arduino: same API, different two lines of setup.
#include <gsm/modem/modem.hpp>
#include <gsm_port_arduino.hpp>

using namespace gsm;

port::ArduinoUart  modem_uart(Serial1);
port::ArduinoClock clk;
Modem              modem(modem_uart, clk, kSim800);

void onSms(void*, const SmsMessage& m)
{
    Serial.print("SMS from ");
    Serial.print(m.number.c_str());
    Serial.print(": ");
    Serial.println(m.text.c_str());
}

void setup()
{
    Serial.begin(115200);
    Serial1.begin(115200);
    log::Registry::set_color(false);
    modem.set_on_sms(SmsService::Received(&onSms, nullptr));
    modem.begin();
}

void loop()
{
    modem.tick();
    static bool sent = false;
    if (!sent && modem.ready())
    {
        sent = true;
        modem.send_sms("09037744527", "سلام از آردوینو");
    }
}
