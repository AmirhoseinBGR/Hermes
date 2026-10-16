#include "gsm/codec/ucs2.hpp"
#include "gsm/modem/modem.hpp"
#include "gsm_port_host.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace gsm;

static port::HostUart<2048>   uart;
static port::HostClock        clock_;
static port::HostPowerControl pwr;
static Modem                  modem(uart, clock_, kSim800, &pwr);

static void advance(uint32_t ms, uint8_t rounds = 4)
{
    for (uint8_t i = 0; i < rounds; ++i)
    {
        clock_.now += ms;
        modem.tick();
    }
}

static bool tx_has(const char* needle)
{ return std::strstr(uart.tx(), needle); }

int main()
{
    printf("Testing codecs & operator detection...\n");
    char hex[128];
    assert(codec::utf8_to_ucs2_hex("سلام", hex, sizeof(hex)) == 16);
    assert(std::strcmp(hex, "0633064406270645") == 0);

    assert(detect_operator("IRANCELL") == Operator::Irancell);
    assert(detect_operator("MTN Irancell") == Operator::Irancell);
    assert(detect_operator("IR-MCI") == Operator::Mci);
    assert(detect_operator("RighTel") == Operator::Rightel);

    assert(std::strcmp(UssdService::default_balance_code(Operator::Irancell), "*141*1#") == 0);
    assert(std::strcmp(UssdService::default_balance_code(Operator::Mci), "*140*11#") == 0);
    assert(std::strcmp(UssdService::default_balance_code(Operator::Rightel), "*140#") == 0);

    printf("Testing boot sequence...\n");
    modem.maintenance().policy().health_check = false; // tested on demand later
    modem.begin();
    advance(50, 1);
    assert(tx_has("AT"));
    uart.inject("OK\r\n");
    advance(50, 2);
    assert(modem.state() == ModemState::WaitingRegistration);

    // Wait for registration
    uart.clear_tx();
    advance(100, 2);
    uart.inject("\r\n+CREG: 0,1\r\n\r\nOK\r\n");
    advance(50, 2);

    // Answer bootstrap commands
    for (int i = 0; i < 15; ++i)
    {
        advance(40, 1);
        uart.inject("OK\r\n");
        advance(40, 1);
    }
    printf("State after bootstrap: %s\n", modem.state_name());
    assert(modem.ready());

    // Test USSD query
    uart.clear_tx();
    JobId ussd_job = modem.check_balance();
    assert(ussd_job != kInvalidJob);
    advance(40, 2);
    assert(tx_has("AT+CUSD="));
    uart.inject("OK\r\n");
    advance(40, 2);
    uart.inject("\r\n+CUSD: 0,\"Mojoodi: 50000 Rials\",15\r\n\r\nOK\r\n");
    advance(40, 3);
    assert(modem.current_job().kind == JobKind::None);
    printf("USSD response received successfully!\n");

    // ---- SMS strict length guard ----
    printf("Testing SMS strict length guard...\n");
    modem.housekeeping().enabled              = false; // deterministic AT traffic
    modem.maintenance().policy().health_check = false; // health runs on demand below
    modem.maintenance().cancel_check();
    uart.clear_tx();
    bool        too_long_seen = false;
    JobCallback guard_cb(
        [](void* ctx, const Job& j) {
            if (j.state == JobState::Failed && j.result.code == kErrSmsTooLong)
                *static_cast<bool*>(ctx) = true;
        },
        &too_long_seen);
    char long_text[200];
    std::memset(long_text, 'a', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';
    JobId bad                        = modem.send_sms("09121112233", long_text, guard_cb);
    assert(bad == kInvalidJob);
    assert(too_long_seen);
    advance(50, 2);
    assert(!tx_has("AT+CMGS"));
    // limit is runtime adjustable
    too_long_seen = false;
    modem.sms().set_max_chars(3);
    assert(modem.send_sms("09121112233", "hello", guard_cb) == kInvalidJob);
    assert(too_long_seen);
    modem.sms().set_max_chars(GSM_SMS_MAX_CHARS);
    // short texts still pass
    JobId good = modem.send_sms("09121112233", "hello", guard_cb);
    assert(good != kInvalidJob);
    assert(modem.cancel(good));
    printf("SMS length guard works!\n");

    // ---- call supervision: 20s auto-hangup on unanswered outgoing call ----
    printf("Testing call supervision: auto-hangup after 20s...\n");
    uart.clear_tx();
    JobId dial_job = modem.dial("09121112233");
    assert(dial_job != kInvalidJob);
    advance(50, 3);
    if (tx_has("AT+CLIP=1"))
    {
        uart.inject("OK\r\n");
        advance(50, 3);
    }
    assert(tx_has("ATD+989121112233;"));
    uart.inject("OK\r\n");
    advance(50, 3);
    assert(modem.call().state() == CallState::Outgoing);

    bool ath_sent = false;
    for (int i = 0; i < 30 && !ath_sent; ++i)
    {
        uart.clear_tx();
        advance(1000, 3);
        if (tx_has("AT+CHUP") || tx_has("ATH"))
        {
            ath_sent = true;
            uart.inject("OK\r\n");
            advance(50, 2);
        }
        else if (tx_has("AT+CLCC"))
        {
            // call keeps ringing with no answer (alerting state 3)
            uart.inject("\r\n+CLCC: 1,0,3,0,0\r\n\r\nOK\r\n");
            advance(50, 2);
        }
    }
    assert(ath_sent);
    assert(modem.call().state() == CallState::Idle);
    assert(modem.call().last_end_reason() == CallEndReason::Timeout);
    printf("Auto-hangup after timeout works!\n");

    // ---- missed end-URC detection + call gate (queue must survive a call) ----
    printf("Testing missed call-end detection & queue gate...\n");
    uart.clear_tx();
    modem.dial("09121112233");
    advance(50, 3);
    if (tx_has("AT+CLIP=1"))
    {
        uart.inject("OK\r\n");
        advance(50, 3);
    }
    assert(tx_has("ATD+989121112233;"));
    uart.inject("OK\r\n");
    advance(50, 3);
    assert(modem.call().state() == CallState::Outgoing);

    // SMS queued while a call is engaged must be held (no SMS AT traffic)
    uart.clear_tx();
    JobId held = modem.send_sms("09121112233", "held-msg");
    assert(held != kInvalidJob);
    advance(200, 3);
    assert(!tx_has("AT+CMGS"));

    // the module never sends NO CARRIER / BUSY (M08R style): empty +CLCC
    // results must reveal that the call is gone
    bool rejected = false;
    for (int i = 0; i < 10 && !rejected; ++i)
    {
        uart.clear_tx();
        advance(1000, 3);
        if (tx_has("AT+CHUP") || tx_has("ATH"))
        {
            uart.inject("OK\r\n");
            advance(50, 2);
            rejected = (modem.call().state() == CallState::Idle);
        }
        else if (tx_has("AT+CLCC"))
        {
            uart.inject("\r\nOK\r\n"); // empty call list
            advance(50, 2);
        }
    }
    assert(rejected);
    assert(modem.call().last_end_reason() == CallEndReason::NoAnswer);

    // after the call ends the queue must move again
    bool sms_started = tx_has("AT+CMGS") || tx_has("AT+CMGF");
    for (int i = 0; i < 6 && !sms_started; ++i)
    {
        advance(200, 3);
        sms_started = tx_has("AT+CMGS") || tx_has("AT+CMGF");
    }
    assert(sms_started);
    modem.stop_all(false);
    printf("Queue survives calls & missed end-URCs are detected!\n");

    // ---- dual maintenance resetters (budget logic) ----
    printf("Testing dual maintenance resetters...\n");
    {
        MaintenanceWatchdog wd;
        wd.policy().periodic_ms = 1000;
        wd.policy().ops_period  = 3;
        wd.on_reset_started(0);
        assert(!wd.reset_pending());
        wd.tick(1001); // time budget fires
        assert(wd.reset_pending());
        assert(wd.take_pending() == ResetReason::Periodic);
        wd.on_reset_started(2000);
        assert(!wd.reset_pending());
        wd.note_operation();
        wd.note_operation();
        wd.tick(2500);
        assert(!wd.reset_pending()); // only 2/3 ops
        wd.note_operation();
        wd.tick(2600);
        assert(wd.reset_pending());
        assert(wd.take_pending() == ResetReason::OpsBudget);
        wd.request_reset(ResetReason::Manual);
        assert(wd.take_pending() == ResetReason::Manual);
    }
    printf("Both resetter budgets work!\n");

    // ---- health check after reset ----
    printf("Testing health check...\n");
    uart.clear_tx();
    modem.maintenance().start_check();
    for (int i = 0; i < 12; ++i)
    {
        advance(50, 2);
        if (tx_has("AT+CSQ"))
        {
            uart.clear_tx();
            uart.inject("\r\n+CSQ: 20,0\r\n\r\nOK\r\n");
            advance(50, 2);
        }
        else if (tx_has("AT+CPIN?"))
        {
            uart.clear_tx();
            uart.inject("\r\nOK\r\n"); // SIM state comes from live status
            advance(50, 2);
        }
        else if (tx_has("AT+CREG?"))
        {
            uart.clear_tx();
            uart.inject("\r\nOK\r\n");
            advance(50, 2);
        }
        else if (tx_has("AT"))
        {
            uart.clear_tx();
            uart.inject("OK\r\n");
            advance(50, 2);
        }
        if (modem.maintenance().health().state != HealthState::Checking &&
            modem.maintenance().health().state != HealthState::Unknown)
            break;
    }
    // SIM / network live flags feed the verdict
    assert(modem.maintenance().health().at_ok);
    assert(modem.maintenance().health().rssi_dbm == -113 + 2 * 20);
    printf("Health check probes work!\n");

    // Test SIM removal URC
    bool sim_removed_detected = false;
    modem.set_on_sim(Modem::SimEventCb(
        [](void* ctx, SimState s) {
            if (s == SimState::Removed) *static_cast<bool*>(ctx) = true;
        },
        &sim_removed_detected));

    uart.inject("\r\n+CPIN: NOT INSERTED\r\n");
    advance(40, 2);
    assert(modem.state() == ModemState::SimMissing);
    assert(sim_removed_detected);
    printf("SIM removal detected!\n");

    // Test SIM re-insertion recovery via CFUN cycle
    // (recovery timing is clock based, so drive it by observed commands)
    bool cfun0 = false, cfun1 = false;
    for (int i = 0; i < 80 && !(cfun0 && cfun1); ++i)
    {
        uart.clear_tx();
        advance(500, 2);
        if (tx_has("AT+CFUN=0"))
        {
            cfun0 = true;
            uart.inject("OK\r\n");
            advance(40, 2);
        }
        else if (tx_has("AT+CFUN=1"))
        {
            cfun1 = true;
            uart.inject("OK\r\n");
            advance(40, 2);
        }
    }
    assert(cfun0 && cfun1);
    // Now simulate SIM ready after CFUN reset
    uart.clear_tx();
    uart.inject("\r\n+CPIN: READY\r\n");
    advance(40, 2);
    assert(modem.state() == ModemState::WaitingRegistration);
    printf("SIM re-insertion handled with CFUN cycle, waiting for registration!\n");

    // Test Watchdog: Queue purging stale jobs
    printf("Testing Watchdog & Queue Purge...\n");
    modem.pause_queue();
    JobId job1 = modem.send_sms("09121112233", "msg1");
    assert(job1 != kInvalidJob);
    assert(modem.queue_size() == 1);
    // Fast forward past GSM_JOB_MAX_AGE_MS (45000ms)
    advance(46000, 2);
    assert(modem.queue_size() == 0);
    printf("Stale job successfully purged by watchdog!\n");

    // Test Watchdog: Consecutive timeouts hardware power cycle
    printf("Testing Watchdog Consecutive Timeouts...\n");
    modem.resume_queue();
    uint32_t initial_pulses = pwr.pulse_count;
    for (int i = 0; i < 3; ++i)
    {
        uart.clear_tx();
        modem.send_sms("09121112233", "fail");
        advance(11000, 2); // Cause send timeout
    }
    assert(pwr.pulse_count > initial_pulses);
    printf("Watchdog triggered hardware power cycle on consecutive timeouts!\n");

    printf("ALL TESTS PASSED!\n");
    return 0;
}
