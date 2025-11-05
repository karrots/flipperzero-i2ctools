#pragma once

#include <stdint.h>

#include <gui/gui.h>

typedef struct CryptoView CryptoView;

CryptoView* crypto_view_alloc(void);
void crypto_view_free(CryptoView* view);

void crypto_view_enter(CryptoView* view);
void crypto_view_exit(CryptoView* view);

void crypto_view_select_previous(CryptoView* view);
void crypto_view_select_next(CryptoView* view);
void crypto_view_execute_selected(CryptoView* view);

void crypto_view_adjust_slot(CryptoView* view, int8_t delta);
uint8_t crypto_view_current_slot(const CryptoView* view);

void draw_crypto_view(Canvas* canvas, CryptoView* view);
