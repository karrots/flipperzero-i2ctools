#pragma once

#include <furi.h>
#include <stdbool.h>

#if defined(__has_include)
#    if __has_include("../lib/cryptoauthlib/lib/cryptoauthlib.h")
#        define I2C_TOOLS_HAS_CRYPTOAUTHLIB 1
#    else
#        define I2C_TOOLS_HAS_CRYPTOAUTHLIB 0
#    endif
#else
#    define I2C_TOOLS_HAS_CRYPTOAUTHLIB 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    I2C_CRYPTO_STATUS_OK = 0,
    I2C_CRYPTO_STATUS_UNAVAILABLE,
    I2C_CRYPTO_STATUS_ERROR,
} I2CCryptoStatus;

typedef struct I2CCryptoService I2CCryptoService;

bool i2c_crypto_is_available(void);
I2CCryptoService* i2c_crypto_service_alloc(void);
void i2c_crypto_service_free(I2CCryptoService* service);
I2CCryptoStatus i2c_crypto_session_begin(I2CCryptoService* service, uint8_t address);
void i2c_crypto_session_end(I2CCryptoService* service);

#ifdef __cplusplus
}
#endif

