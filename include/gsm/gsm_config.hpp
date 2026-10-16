#pragma once

#if defined(GSM_USER_CONFIG)
#include GSM_USER_CONFIG
#endif

// --- buffers -----------------------------------------------------------------
#ifndef GSM_RX_RING_SIZE
#define GSM_RX_RING_SIZE 1024
#endif
#ifndef GSM_LINE_MAX
#define GSM_LINE_MAX 512
#endif
#ifndef GSM_CMD_MAX
#define GSM_CMD_MAX 256
#endif

// --- sms & ussd --------------------------------------------------------------
#ifndef GSM_SMS_TEXT_BYTES
#define GSM_SMS_TEXT_BYTES 512
#endif
// Strict outgoing length guard (in UTF-8 characters). A single UCS2 SMS carries
// 70 chars; the default keeps a hard safety margin below that. Runtime adjustable
// via Modem::sms_policy().max_chars (0 disables the guard).
#ifndef GSM_SMS_MAX_CHARS
#define GSM_SMS_MAX_CHARS 60
#endif
#ifndef GSM_SMS_NUMBER_BYTES
#define GSM_SMS_NUMBER_BYTES 24
#endif
#ifndef GSM_SMS_INBOX_SLOTS
#define GSM_SMS_INBOX_SLOTS 4
#endif
#ifndef GSM_USSD_RESPONSE_BYTES
#define GSM_USSD_RESPONSE_BYTES 512
#endif

// --- queue -------------------------------------------------------------------
#ifndef GSM_QUEUE_DEPTH
#define GSM_QUEUE_DEPTH 6
#endif
#ifndef GSM_JOB_MAX_AGE_MS
#define GSM_JOB_MAX_AGE_MS 45000 // drop stale queued job if not started within 45s
#endif
#ifndef GSM_JOB_START_RETRIES
#define GSM_JOB_START_RETRIES 3 // service refusals before the job is failed (no stall)
#endif

// --- timing (ms) -------------------------------------------------------------
#ifndef GSM_AT_DEFAULT_TIMEOUT
#define GSM_AT_DEFAULT_TIMEOUT 1000
#endif
#ifndef GSM_AT_PROMPT_TIMEOUT
#define GSM_AT_PROMPT_TIMEOUT 5000
#endif
#ifndef GSM_AT_CMGS_TIMEOUT
#define GSM_AT_CMGS_TIMEOUT 10000
#endif
#ifndef GSM_AT_USSD_TIMEOUT
#define GSM_AT_USSD_TIMEOUT 10000
#endif
#ifndef GSM_AT_GUARD_TIME
#define GSM_AT_GUARD_TIME 30
#endif
#ifndef GSM_REGISTRATION_TIMEOUT
#define GSM_REGISTRATION_TIMEOUT 45000
#endif
#ifndef GSM_PWRKEY_PULSE_MS
#define GSM_PWRKEY_PULSE_MS 1200 // PWRKEY low time (SIM800/M08 family datasheet minimum)
#endif
#ifndef GSM_MODEM_BOOT_GUARD_MS
#define GSM_MODEM_BOOT_GUARD_MS 2500 // silence after a PWRKEY pulse before AT probing
#endif

// --- call supervision --------------------------------------------------------
#ifndef GSM_CALL_ANSWER_TIMEOUT_MS
#define GSM_CALL_ANSWER_TIMEOUT_MS 20000 // MO call: auto-hangup if not established in this window
#endif
#ifndef GSM_CALL_RING_TIMEOUT_MS
#define GSM_CALL_RING_TIMEOUT_MS 20000 // MT call: auto-reject if not answered in this window
#endif
#ifndef GSM_CALL_CLCC_POLL_MS
#define GSM_CALL_CLCC_POLL_MS 2000 // +CLCC poll interval while a call is non-idle
#endif
#ifndef GSM_CALL_CLCC_MISSES
#define GSM_CALL_CLCC_MISSES 2 // empty +CLCC results before the call is declared dead
#endif

// --- industrial watchdog & recovery ------------------------------------------
#ifndef GSM_CONSECUTIVE_TIMEOUTS_RESET
#define GSM_CONSECUTIVE_TIMEOUTS_RESET 3 // consecutive AT timeouts before triggering recovery
#endif
#ifndef GSM_SIM_RETRY_INTERVAL_MS
#define GSM_SIM_RETRY_INTERVAL_MS 10000 // when SIM missing, re-probe / cycle CFUN periodically
#endif
#ifndef GSM_CFUN_CYCLE_INTERVAL_MS
#define GSM_CFUN_CYCLE_INTERVAL_MS 25000 // if SIM still missing, cycle CFUN=0 then CFUN=1
#endif
#ifndef GSM_HARDWARE_REBOOT_TIMEOUT_MS
#define GSM_HARDWARE_REBOOT_TIMEOUT_MS                                                             \
    90000 // if stuck without SIM/FSM for 90s, do full power cycle
#endif

// --- maintenance resetters (dual watchdog) -----------------------------------
#ifndef GSM_PERIODIC_RESET_MS
#define GSM_PERIODIC_RESET_MS 3600000 // resetter #1: full module reset every 60 min (0=off)
#endif
#ifndef GSM_OPS_RESET_COUNT
#define GSM_OPS_RESET_COUNT 50 // resetter #2: full reset after N operations (0=off)
#endif
#ifndef GSM_WATCHDOG_GRACE_MS
#define GSM_WATCHDOG_GRACE_MS 10000 // force the reset if no safe window appears within this
#endif

// --- logging -----------------------------------------------------------------
#ifndef GSM_LOG_LEVEL_MAX
#define GSM_LOG_LEVEL_MAX 4 // 0=off 1=err 2=warn 3=info 4=debug 5=trace
#endif
#ifndef GSM_LOG_BUFFER_SIZE
#define GSM_LOG_BUFFER_SIZE 180
#endif
#ifndef GSM_LOG_COLOR_DEFAULT
#define GSM_LOG_COLOR_DEFAULT 1
#endif
