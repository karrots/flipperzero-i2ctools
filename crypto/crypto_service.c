#include "crypto_service.h"

#include <furi.h>
#include <furi_hal.h>

#define CRYPTO_DEFAULT_I2C_ADDRESS (0x60U)
#define CRYPTO_WAKE_DELAY_US (1600U)
#define CRYPTO_RX_RETRIES (20U)

static void crypto_populate_default_cfg(ATCAIfaceCfg* cfg) {
    furi_assert(cfg);
    cfg->i2c_address = CRYPTO_DEFAULT_I2C_ADDRESS;
    cfg->bus_khz = 100;
    cfg->wake_delay = CRYPTO_WAKE_DELAY_US;
    cfg->rx_retries = CRYPTO_RX_RETRIES;
}

void crypto_session_init(CryptoSession* session) {
    furi_assert(session);
    crypto_populate_default_cfg(&session->iface_cfg);
    session->is_active = false;
}

bool crypto_session_begin(CryptoSession* session) {
    furi_assert(session);

    if(session->is_active) {
        return true;
    }

    crypto_populate_default_cfg(&session->iface_cfg);

    ATCA_STATUS status = atcab_init(&session->iface_cfg);
    if(status != ATCA_STATUS_SUCCESS) {
        return false;
    }

    status = atcab_wakeup();
    if(status != ATCA_STATUS_SUCCESS) {
        atcab_release();
        return false;
    }

    session->is_active = true;
    return true;
}

void crypto_session_end(CryptoSession* session, CryptoDevicePowerState power_state) {
    furi_assert(session);

    if(!session->is_active) {
        return;
    }

    if(power_state == CryptoDeviceSleep) {
        atcab_sleep();
    } else {
        atcab_idle();
    }

    atcab_release();
    session->is_active = false;
}

ATCA_STATUS crypto_run_command(
    CryptoSession* session,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t rx_max,
    size_t* rx_len) {
    furi_assert(session);

    if(!session->is_active) {
        return ATCA_STATUS_NOT_INITIALIZED;
    }

    return atcab_transceive(tx, tx_len, rx, rx_max, rx_len);
}

bool crypto_info_smoke_test(uint8_t* revision_buffer, size_t buffer_size) {
    if((revision_buffer == NULL) || (buffer_size < ATCA_INFO_SIZE)) {
        return false;
    }

    CryptoSession session;
    crypto_session_init(&session);

    if(!crypto_session_begin(&session)) {
        return false;
    }

    ATCA_STATUS status = atcab_info(revision_buffer);
    crypto_session_end(&session, CryptoDeviceSleep);

    return status == ATCA_STATUS_SUCCESS;
}

