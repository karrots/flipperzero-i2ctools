#pragma once

#include <furi.h>
#include <furi_hal.h>
#include "i2cscanner.h"

#define I2C_SENDER_RECV_BUFFER_SIZE 128

typedef enum {
    I2C_Sender_FocusValue = 0,
    I2C_Sender_FocusLength,
    I2C_Sender_FocusResult,
    I2C_Sender_FocusCount,
} I2CSenderFocus;

typedef struct {
    uint8_t address_idx;
    uint8_t value;
    uint8_t recv[I2C_SENDER_RECV_BUFFER_SIZE];
    uint8_t recv_len;
    uint8_t requested_len;
    uint8_t result_row_offset;
    I2CSenderFocus focus;
    bool must_send;
    bool sended;
    bool error;

    i2cScanner* scanner;
} i2cSender;

void i2c_send(i2cSender* i2c_sender);

i2cSender* i2c_sender_alloc();
void i2c_sender_free(i2cSender* i2c_sender);
