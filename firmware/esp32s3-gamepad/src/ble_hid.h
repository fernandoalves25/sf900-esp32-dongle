#pragma once
#include <stdint.h>
#include <stdbool.h>

// Inicia o gamepad BLE HID (HID-over-GATT). Anuncia como "SF900 Gamepad".
void ble_hid_init(void);

// Atualiza o estado dos botoes a partir do valor "raw" do SF900
// (mesmos bits do USB). Envia por BLE se houver host conectado.
void ble_hid_update(uint32_t raw);

// true se algum host BLE estiver conectado
bool ble_hid_connected(void);
