#pragma once
#include "gsm/gsm_config.hpp"
#include <cstdint>

namespace gsm
{

struct DeviceTraits
{
    const char* name;
    bool        ucs2_recipient_required;
    const char* csmp_ucs2;
    const char* csmp_gsm;
    const char* hangup_cmd;
    const char* storage_cmd;
    bool        supports_clcc;
    bool        supports_cmgd_delete_all;
    uint32_t    boot_timeout_ms;
    uint32_t    prompt_timeout_ms;
    uint32_t    send_timeout_ms;
    // If true, call supervision trusts +CLCC polling as the primary source of
    // truth (module URCs for NO CARRIER / BUSY are unreliable, e.g. M08R).
    bool clcc_authoritative;
};

inline constexpr DeviceTraits kSim800{"SIM800",
                                      /*ucs2_recipient_required*/ true,
                                      /*csmp_ucs2*/ "AT+CSMP=17,71,0,25",
                                      /*csmp_gsm*/ "AT+CSMP=17,167,0,0",
                                      /*hangup*/ "ATH",
                                      /*storage*/ "AT+CPMS=\"SM\",\"SM\",\"SM\"",
                                      /*clcc*/ true,
                                      /*cmgd_all*/ true,
                                      /*boot*/ 15000,
                                      /*prompt*/ GSM_AT_PROMPT_TIMEOUT,
                                      /*send*/ GSM_AT_CMGS_TIMEOUT,
                                      /*clcc_auth*/ false};

inline constexpr DeviceTraits kQuectelM08{"M08",
                                          /*ucs2_recipient_required*/ true,
                                          /*csmp_ucs2*/ "AT+CSMP=17,167,0,8",
                                          /*csmp_gsm*/ "AT+CSMP=17,167,0,0",
                                          /*hangup*/ "AT+CHUP",
                                          /*storage*/ "AT+CPMS=\"ME\",\"ME\",\"ME\"",
                                          /*clcc*/ true,
                                          /*cmgd_all*/ true,
                                          /*boot*/ 20000,
                                          /*prompt*/ GSM_AT_PROMPT_TIMEOUT,
                                          /*send*/ GSM_AT_CMGS_TIMEOUT,
                                          /*clcc_auth*/ false};

// M08R variant: call-end URCs (NO CARRIER / BUSY / NO ANSWER) are unreliable and
// the module can wedge after interrupted SMS payloads. Call supervision therefore
// relies on +CLCC polling (clcc_authoritative) plus the call watchdog.
inline constexpr DeviceTraits kQuectelM08R{"M08R",
                                           /*ucs2_recipient_required*/ true,
                                           /*csmp_ucs2*/ "AT+CSMP=17,167,0,8",
                                           /*csmp_gsm*/ "AT+CSMP=17,167,0,0",
                                           /*hangup*/ "AT+CHUP",
                                           /*storage*/ "AT+CPMS=\"ME\",\"ME\",\"ME\"",
                                           /*clcc*/ true,
                                           /*cmgd_all*/ true,
                                           /*boot*/ 20000,
                                           /*prompt*/ GSM_AT_PROMPT_TIMEOUT,
                                           /*send*/ GSM_AT_CMGS_TIMEOUT,
                                           /*clcc_auth*/ true};

} // namespace gsm
