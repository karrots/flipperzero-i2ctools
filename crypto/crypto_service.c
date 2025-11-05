#include "crypto_service.h"

#include "../lib/cryptoauthlib/lib/atca_basic.h"
#include "../lib/cryptoauthlib/lib/calib/calib_command.h"
#include "../lib/cryptoauthlib/lib/calib/calib_packet.h"
#include "../lib/cryptoauthlib/lib/calib/calib_execution.h"

#include <furi.h>
#include <furi_hal.h>
#include <string.h>

#define CRYPTO_DEFAULT_I2C_ADDRESS (0x60U)
#define CRYPTO_WAKE_DELAY_US       (1600U)
#define CRYPTO_RX_RETRIES          (20U)

static void crypto_populate_default_cfg(ATCAIfaceCfg* cfg) {
    furi_assert(cfg);
    memset(cfg, 0, sizeof(*cfg));

    cfg->iface_type = ATCA_I2C_IFACE;
    cfg->devtype = ATECC608;
#ifdef ATCA_ENABLE_DEPRECATED
    cfg->atcai2c.slave_address = (uint8_t)(CRYPTO_DEFAULT_I2C_ADDRESS << 1U);
#else
    cfg->atcai2c.address = (uint8_t)(CRYPTO_DEFAULT_I2C_ADDRESS << 1U);
#endif
    cfg->atcai2c.bus = 0;
    cfg->atcai2c.baud = 100000U;
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
    if(status != ATCA_SUCCESS) {
        return false;
    }

    status = atcab_wakeup();
    if(status != ATCA_SUCCESS) {
        atcab_release();
        return false;
    }

    // Allow device to settle after wakeup before processing commands
    // Some commands (Info, Config reads) need more time than others
    furi_delay_ms(10);

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
        return ATCA_NOT_INITIALIZED;
    }

    if((tx == NULL) || (tx_len < ATCA_CMD_SIZE_MIN)) {
        return ATCA_BAD_PARAM;
    }

    if(tx_len > CA_MAX_PACKET_SIZE) {
        return ATCA_INVALID_SIZE;
    }

    const uint8_t count = tx[ATCA_COUNT_IDX];
    if(count != tx_len) {
        return ATCA_INVALID_SIZE;
    }

    ATCADevice device = atcab_get_device();
    if(device == NULL) {
        return ATCA_NOT_INITIALIZED;
    }

    ATCAPacket* packet = calib_packet_alloc();
    if(packet == NULL) {
        return ATCA_ALLOC_FAILURE;
    }

    memset(packet, 0, sizeof(*packet));
    memcpy(&packet->txsize, tx, tx_len);

    ATCA_STATUS status = calib_execute_command(packet, device);

    if(status == ATCA_SUCCESS) {
        if(rx_len != NULL) {
            *rx_len = 0;
        }

        if(rx != NULL && rx_len != NULL) {
            const uint8_t response_len = packet->data[ATCA_COUNT_IDX];

            if(response_len > rx_max) {
                status = ATCA_SMALL_BUFFER;
            } else {
                memcpy(rx, packet->data, response_len);
                *rx_len = response_len;
            }
        }
    }

    calib_packet_free(packet);
    return status;
}

bool crypto_info_smoke_test(uint8_t* revision_buffer, size_t buffer_size) {
    if((revision_buffer == NULL) || (buffer_size < INFO_SIZE)) {
        return false;
    }

    CryptoSession session;
    crypto_session_init(&session);

    if(!crypto_session_begin(&session)) {
        return false;
    }

    ATCA_STATUS status = atcab_info(revision_buffer);
    crypto_session_end(&session, CryptoDeviceSleep);

    return status == ATCA_SUCCESS;
}
