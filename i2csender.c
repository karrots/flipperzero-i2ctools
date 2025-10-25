#include "i2csender.h"

#include <stdlib.h>
#include <string.h>

void i2c_send(i2cSender* i2c_sender) {
    furi_assert(i2c_sender);
    memset(i2c_sender->recv, 0, sizeof(i2c_sender->recv));
    i2c_sender->recv_len = 0;
    i2c_sender->result_row_offset = 0;
    uint8_t rx_len = i2c_sender->requested_len;
    if(rx_len == 0) {
        rx_len = 1;
    } else if(rx_len > I2C_SENDER_RECV_BUFFER_SIZE) {
        rx_len = I2C_SENDER_RECV_BUFFER_SIZE;
    }
    furi_hal_i2c_acquire(I2C_BUS);
    uint8_t adress = i2c_sender->scanner->addresses[i2c_sender->address_idx] << 1;
    bool success = furi_hal_i2c_trx(
        I2C_BUS,
        adress,
        &i2c_sender->value,
        sizeof(i2c_sender->value),
        i2c_sender->recv,
        rx_len,
        I2C_TIMEOUT);
    furi_hal_i2c_release(I2C_BUS);
    i2c_sender->error = !success;
    if(success) {
        i2c_sender->recv_len = rx_len;
    }
    i2c_sender->must_send = false;
    i2c_sender->sended = true;
}

i2cSender* i2c_sender_alloc() {
    i2cSender* i2c_sender = malloc(sizeof(i2cSender));
    furi_assert(i2c_sender);
    memset(i2c_sender, 0, sizeof(i2cSender));
    i2c_sender->requested_len = 32;
    i2c_sender->focus = I2C_Sender_FocusValue;
    return i2c_sender;
}

void i2c_sender_free(i2cSender* i2c_sender) {
    furi_assert(i2c_sender);
    free(i2c_sender);
}