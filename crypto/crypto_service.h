#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../lib/cryptoauthlib/include/cryptoauthlib.h"

typedef enum {
    CryptoDeviceIdle,
    CryptoDeviceSleep,
} CryptoDevicePowerState;

typedef struct {
    ATCAIfaceCfg iface_cfg;
    bool is_active;
} CryptoSession;

void crypto_session_init(CryptoSession* session);
bool crypto_session_begin(CryptoSession* session);
void crypto_session_end(CryptoSession* session, CryptoDevicePowerState power_state);

ATCA_STATUS crypto_run_command(
    CryptoSession* session,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t rx_max,
    size_t* rx_len);

bool crypto_info_smoke_test(uint8_t* revision_buffer, size_t buffer_size);

