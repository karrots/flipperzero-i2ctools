#pragma once

#include <furi.h>
#include <furi_hal.h>
#include "i2cscanner.h"

#define I2C_SENDER_DEFAULT_RECV_LEN 2

typedef struct {
    uint8_t address_idx;
    uint8_t value;
    uint8_t recv[I2C_SENDER_DEFAULT_RECV_LEN];
    bool must_send;
    bool sended;
    bool error;

    i2cScanner* scanner;
} i2cSender;

void i2c_send(i2cSender* i2c_sender);

i2cSender* i2c_sender_alloc();
void i2c_sender_free(i2cSender* i2c_sender);
