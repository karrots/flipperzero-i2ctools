/* Lightweight config for CryptoAuthLib when built inside I2C Tools */
#ifndef ATCA_CONFIG_H
#define ATCA_CONFIG_H

#define ATCA_HAL_I2C
#define ATCA_ATECC608_SUPPORT
#define ATCA_USE_ATCAB_FUNCTIONS
#define ATCA_ENABLE_DEPRECATED
#define ATCA_CHECK_PARAMS_EN       1
#define ATCA_TA_SUPPORT            0
#define ATCAC_SHA384_EN            0
#define ATCAC_SHA512_EN            0
#define ATCACERT_COMPCERT_EN       1  // Enable certificate support for serial number retrieval
#define ATCACERT_FULLSTOREDCERT_EN 0
#define ATCACERT_INTEGRATION_EN    0
#define ATCA_MAX_HAL_CACHE         1
#define ATCA_POST_DELAY_MSEC       25

#endif /* ATCA_CONFIG_H */
