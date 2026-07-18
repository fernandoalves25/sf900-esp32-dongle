#pragma once
#include <stdint.h>
extern volatile uint32_t led_rx_count;  // incremente a cada pacote recebido
void led_rx_start(void);                 // inicia o LED (verde fraco no RX)
