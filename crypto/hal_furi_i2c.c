#include "../lib/cryptoauthlib/hal/hal_furi_i2c.h"

#include <furi_hal.h>
#include <furi_hal_i2c.h>

#define CRYPTO_I2C_TIMEOUT (5U)

static const FuriHalI2cBusHandle* crypto_get_bus(void) {
    return &furi_hal_i2c_handle_external;
}

static uint32_t crypto_timeout_ms(void) {
    return CRYPTO_I2C_TIMEOUT;
}

static bool crypto_bus_locked = false;

static void crypto_acquire_bus(void) {
    if(!crypto_bus_locked) {
        furi_hal_i2c_acquire(crypto_get_bus());
        crypto_bus_locked = true;
    }
}

static void crypto_release_bus(void) {
    if(crypto_bus_locked) {
        furi_hal_i2c_release(crypto_get_bus());
        crypto_bus_locked = false;
    }
}

ATCA_STATUS hal_furi_i2c_wake(const ATCAIfaceCfg* cfg) {
    (void)cfg;
    crypto_acquire_bus();
    furi_delay_us(1500);
    return ATCA_STATUS_SUCCESS;
}

ATCA_STATUS hal_furi_i2c_idle(const ATCAIfaceCfg* cfg) {
    (void)cfg;
    crypto_release_bus();
    return ATCA_STATUS_SUCCESS;
}

ATCA_STATUS hal_furi_i2c_sleep(const ATCAIfaceCfg* cfg) {
    (void)cfg;
    crypto_release_bus();
    return ATCA_STATUS_SUCCESS;
}

ATCA_STATUS hal_furi_i2c_send(const ATCAIfaceCfg* cfg, const uint8_t* tx, size_t tx_len) {
    if((cfg == NULL) || (tx == NULL) || (tx_len == 0U)) {
        return ATCA_STATUS_BAD_PARAM;
    }

    crypto_acquire_bus();

    bool ok = furi_hal_i2c_tx(
        crypto_get_bus(),
        (uint8_t)(cfg->i2c_address << 1U),
        tx,
        tx_len,
        crypto_timeout_ms());

    if(!ok) {
        return ATCA_STATUS_COMM_FAIL;
    }

    return ATCA_STATUS_SUCCESS;
}

ATCA_STATUS hal_furi_i2c_receive(
    const ATCAIfaceCfg* cfg,
    uint8_t* rx,
    size_t rx_max,
    size_t* rx_len) {
    if((cfg == NULL) || (rx == NULL) || (rx_max == 0U) || (rx_len == NULL)) {
        return ATCA_STATUS_BAD_PARAM;
    }

    crypto_acquire_bus();

    bool ok = furi_hal_i2c_rx(
        crypto_get_bus(),
        (uint8_t)(cfg->i2c_address << 1U),
        rx,
        rx_max,
        crypto_timeout_ms());

    if(!ok) {
        *rx_len = 0U;
        return ATCA_STATUS_COMM_FAIL;
    }

    *rx_len = rx_max;
    return ATCA_STATUS_SUCCESS;
}

