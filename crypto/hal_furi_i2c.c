#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_i2c.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/cryptoauthlib/lib/atca_basic.h"

#include "../lib/cryptoauthlib/lib/atca_iface.h"
#include "../lib/cryptoauthlib/lib/hal/atca_hal.h"
#include "../lib/cryptoauthlib/lib/calib/calib_command.h"

#define CRYPTO_I2C_TIMEOUT_MS (20U)

typedef struct {
    bool bus_locked;
} CryptoHalI2cCtx;

static const FuriHalI2cBusHandle* crypto_bus_handle(void) {
    return &furi_hal_i2c_handle_external;
}

static CryptoHalI2cCtx* crypto_get_ctx(ATCAIface iface) {
    return (CryptoHalI2cCtx*)atgetifacehaldat(iface);
}

static uint8_t crypto_iface_address(const ATCAIfaceCfg* cfg) {
#ifdef ATCA_ENABLE_DEPRECATED
    return (uint8_t)(ATCA_IFACECFG_VALUE(cfg, atcai2c.slave_address));
#else
    return (uint8_t)(ATCA_IFACECFG_VALUE(cfg, atcai2c.address));
#endif
}

static void crypto_acquire_bus(CryptoHalI2cCtx* ctx) {
    if((ctx != NULL) && !ctx->bus_locked) {
        furi_hal_i2c_acquire(crypto_bus_handle());
        ctx->bus_locked = true;
    }
}

static void crypto_release_bus(CryptoHalI2cCtx* ctx) {
    if((ctx != NULL) && ctx->bus_locked) {
        furi_hal_i2c_release(crypto_bus_handle());
        ctx->bus_locked = false;
    }
}

ATCA_STATUS hal_i2c_init(ATCAIface iface, ATCAIfaceCfg* cfg) {
    if((iface == NULL) || (cfg == NULL)) {
        return ATCA_BAD_PARAM;
    }

    if(iface->hal_data != NULL) {
        return ATCA_SUCCESS;
    }

    CryptoHalI2cCtx* ctx = malloc(sizeof(CryptoHalI2cCtx));
    if(ctx == NULL) {
        return ATCA_ALLOC_FAILURE;
    }

    ctx->bus_locked = false;
    iface->hal_data = ctx;

    (void)cfg; // configuration handled per call
    return ATCA_SUCCESS;
}

ATCA_STATUS hal_i2c_post_init(ATCAIface iface) {
    (void)iface;
    return ATCA_SUCCESS;
}

ATCA_STATUS hal_i2c_send(ATCAIface iface, uint8_t word_address, uint8_t* txdata, int txlength) {
    if((iface == NULL) || (txlength < 0)) {
        return ATCA_BAD_PARAM;
    }

    ATCAIfaceCfg* cfg = atgetifacecfg(iface);
    CryptoHalI2cCtx* ctx = crypto_get_ctx(iface);
    if((cfg == NULL) || (ctx == NULL)) {
        return ATCA_BAD_PARAM;
    }

    size_t payload_length = (size_t)((txlength > 0) ? txlength : 0);
    size_t total_length = payload_length + ((word_address != 0xFFU) ? 1U : 0U);

    if(total_length == 0U) {
        return ATCA_SUCCESS;
    }

    if(total_length > (size_t)(ATCA_CMD_SIZE_MAX + 1U)) {
        return ATCA_SMALL_BUFFER;
    }

    uint8_t buffer[ATCA_CMD_SIZE_MAX + 1U];
    size_t index = 0;

    if(word_address != 0xFFU) {
        buffer[index++] = word_address;
    }

    if((txdata != NULL) && (payload_length > 0U)) {
        memcpy(&buffer[index], txdata, payload_length);
        index += payload_length;
    }

    crypto_acquire_bus(ctx);

    const bool ok = furi_hal_i2c_tx(
        crypto_bus_handle(),
        crypto_iface_address(cfg),
        buffer,
        index,
        CRYPTO_I2C_TIMEOUT_MS);

    crypto_release_bus(ctx);

    // Give device time to process command/word address before next transaction
    // This is especially important for config zone operations
    if(ok && word_address == 0x00) {
        furi_delay_us(100);
    }

    return ok ? ATCA_SUCCESS : ATCA_COMM_FAIL;
}

ATCA_STATUS hal_i2c_receive(ATCAIface iface, uint8_t word_address, uint8_t* rxdata, uint16_t* rxlength) {
    if((iface == NULL) || (rxdata == NULL) || (rxlength == NULL)) {
        return ATCA_BAD_PARAM;
    }

    ATCAIfaceCfg* cfg = atgetifacecfg(iface);
    CryptoHalI2cCtx* ctx = crypto_get_ctx(iface);
    if((cfg == NULL) || (ctx == NULL)) {
        return ATCA_BAD_PARAM;
    }

    const uint16_t rx_max = *rxlength;
    if(rx_max == 0U) {
        return ATCA_BAD_PARAM;
    }

    // Zero the receive buffer to ensure clean state
    memset(rxdata, 0, rx_max);

    // Note: word_address parameter is actually the I2C device address to read from
    // The actual word address byte (0x00) is sent separately via hal_i2c_send before this is called
    // Some implementations need this, others get it from config - use word_address if valid
    uint8_t address = (word_address != 0xFFU) ? word_address : crypto_iface_address(cfg);

    crypto_acquire_bus(ctx);

    const bool ok = furi_hal_i2c_rx(
        crypto_bus_handle(),
        address,
        rxdata,
        rx_max,
        CRYPTO_I2C_TIMEOUT_MS);

    crypto_release_bus(ctx);

    if(!ok) {
        *rxlength = 0U;
        return ATCA_COMM_FAIL;
    }

    // When reading the first byte (count byte), check if it's valid
    // For ATECC608, valid response count is always >= 4 (count + data + 2 CRC bytes)
    // If we get 0-3, device is not ready or sending garbage - trigger polling retry
    if(rx_max == 1 && rxdata[0] < 4) {
        *rxlength = 0U;
        return ATCA_RX_NO_RESPONSE;
    }

    if(rxdata[0] <= rx_max) {
        *rxlength = rxdata[0];
    } else {
        *rxlength = rx_max;
    }

    return ATCA_SUCCESS;
}

ATCA_STATUS hal_i2c_control(ATCAIface iface, uint8_t option, void* param, size_t paramlen) {
    (void)param;
    (void)paramlen;

    CryptoHalI2cCtx* ctx = crypto_get_ctx(iface);
    if(ctx == NULL) {
        return ATCA_BAD_PARAM;
    }

    switch(option) {
    case ATCA_HAL_CONTROL_WAKE:
        /* Wake is handled by higher level atcab_wakeup using hal_i2c_receive */
        return ATCA_SUCCESS;
    case ATCA_HAL_CONTROL_IDLE:
    case ATCA_HAL_CONTROL_SLEEP:
        crypto_release_bus(ctx);
        return ATCA_SUCCESS;
    default:
        return ATCA_UNIMPLEMENTED;
    }
}

ATCA_STATUS hal_i2c_release(void* hal_data) {
    if(hal_data == NULL) {
        return ATCA_SUCCESS;
    }

    CryptoHalI2cCtx* ctx = (CryptoHalI2cCtx*)hal_data;
    crypto_release_bus(ctx);
    free(ctx);
    return ATCA_SUCCESS;
}

void* __attribute__((weak)) hal_malloc(size_t size) {
    return malloc(size);
}

void __attribute__((weak)) hal_free(void* ptr) {
    free(ptr);
}

void __attribute__((weak)) atca_delay_us(uint32_t delay) {
    if(delay == 0U) {
        return;
    }
    furi_delay_us(delay);
}

void __attribute__((weak)) atca_delay_ms(uint32_t delay) {
    if(delay == 0U) {
        return;
    }
    furi_delay_ms(delay);
}
