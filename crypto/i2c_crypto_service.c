#include "i2c_crypto_service.h"

#include <furi_hal.h>
#include <stdlib.h>

struct I2CCryptoService {
    bool session_active;
    uint8_t active_address;
};

bool i2c_crypto_is_available(void) {
#if I2C_TOOLS_HAS_CRYPTOAUTHLIB
    return true;
#else
    return false;
#endif
}

I2CCryptoService* i2c_crypto_service_alloc(void) {
    I2CCryptoService* service = malloc(sizeof(I2CCryptoService));
    if(!service) {
        return NULL;
    }
    service->session_active = false;
    service->active_address = 0;
    return service;
}

void i2c_crypto_service_free(I2CCryptoService* service) {
    if(!service) {
        return;
    }
    i2c_crypto_session_end(service);
    free(service);
}

I2CCryptoStatus i2c_crypto_session_begin(I2CCryptoService* service, uint8_t address) {
    furi_assert(service);

    if(service->session_active) {
        if(service->active_address == address) {
            return I2C_CRYPTO_STATUS_OK;
        }
        i2c_crypto_session_end(service);
    }

#if I2C_TOOLS_HAS_CRYPTOAUTHLIB
    UNUSED(address);
    service->session_active = true;
    service->active_address = address;
    return I2C_CRYPTO_STATUS_OK;
#else
    UNUSED(address);
    return I2C_CRYPTO_STATUS_UNAVAILABLE;
#endif
}

void i2c_crypto_session_end(I2CCryptoService* service) {
    if(!service || !service->session_active) {
        return;
    }

#if I2C_TOOLS_HAS_CRYPTOAUTHLIB
    service->session_active = false;
    service->active_address = 0;
#else
    service->session_active = false;
    service->active_address = 0;
#endif
}

