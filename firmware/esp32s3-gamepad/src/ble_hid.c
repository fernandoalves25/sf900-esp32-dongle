/*
 * BLE HID gamepad (HID-over-GATT) para o receptor SF900.
 * Anuncia como "SF900 Gamepad"; pareia em modo "Just Works" (sem senha).
 * Funciona como controle Bluetooth em PC (Windows/Linux), Android, Mac, iOS.
 *
 * Roda em paralelo com o gamepad USB — os botoes vao para os dois.
 * Obs.: o ESP32-S3 tem apenas BLE (nao tem Bluetooth Classic).
 */
#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_hidd.h"
#include "ble_hid.h"

static const char *TAG = "ble_hid";
static esp_hidd_dev_t *s_dev = NULL;
static bool s_connected = false;
static uint8_t s_own_addr_type;

void ble_store_config_init(void);

/* Report map: gamepad com 16 botoes + hat (D-pad). Report ID 1.
 * Dados enviados (3 bytes): [botoes_lo][botoes_hi][hat_nibble] */
static const uint8_t s_report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x10,        //   Usage Maximum (Button 16)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x10,        //   Report Count (16)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x39,        //   Usage (Hat switch)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x07,        //   Logical Maximum (7)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (Eng Rot: Degree)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Const,Var,Abs) - padding
    0xC0               // End Collection
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    { .data = s_report_map, .len = sizeof(s_report_map) },
};

static esp_hid_device_config_t s_hid_cfg = {
    .vendor_id = 0x303A,
    .product_id = 0x4004,
    .version = 0x0100,
    .device_name = "SF900 Gamepad",
    .manufacturer_name = "DataFrog-Transplant",
    .serial_number = "SF900-001",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

/* --- mapeia o raw do SF900 para 16 botoes + hat --- */
static uint8_t dpad_hat(uint32_t raw)
{
    int up=raw&0x0008, dn=raw&0x0004, lf=raw&0x0002, rt=raw&0x0001;
    if (up&&rt) return 1;
    if (rt&&dn) return 3;
    if (dn&&lf) return 5;
    if (lf&&up) return 7;
    if (up) return 0;
    if (rt) return 2;
    if (dn) return 4;
    if (lf) return 6;
    return 0x0F; // centrado (null)
}
static uint16_t btn_bits(uint32_t raw)
{
    uint16_t b=0;
    if (raw&0x0080) b|=1<<0; // A
    if (raw&0x0040) b|=1<<1; // B
    if (raw&0x4000) b|=1<<2; // X
    if (raw&0x2000) b|=1<<3; // Y
    if (raw&0x0800) b|=1<<4; // L
    if (raw&0x1000) b|=1<<5; // R
    if (raw&0x0020) b|=1<<6; // SELECT
    if (raw&0x0010) b|=1<<7; // START
    return b;
}

static void start_advertising(void);

bool ble_hid_connected(void) { return s_connected; }

void ble_hid_update(uint32_t raw)
{
    if (!s_connected || s_dev == NULL) return;
    uint16_t b = btn_bits(raw);
    uint8_t rpt[3] = { (uint8_t)(b & 0xFF), (uint8_t)(b >> 8), dpad_hat(raw) };
    esp_hidd_dev_input_set(s_dev, 0, 1, rpt, sizeof(rpt));
}

/* --- callback de eventos do esp_hidd --- */
static void hidd_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_hidd_event_data_t *p = (esp_hidd_event_data_t *)data;
    switch (id) {
    case ESP_HIDD_START_EVENT:      ESP_LOGI(TAG, "START_EVENT -> advertising"); start_advertising(); break;
    case ESP_HIDD_CONNECT_EVENT:    s_connected = true;  ESP_LOGI(TAG, "host conectado"); break;
    case ESP_HIDD_DISCONNECT_EVENT: s_connected = false; ESP_LOGI(TAG, "host desconectado -> readvertising"); start_advertising(); break;
    default: ESP_LOGI(TAG, "hidd event %ld", (long)id); break;
    }
    (void)p; (void)base; (void)arg;
}

/* --- NimBLE GAP / advertising --- */
static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_gap_adv_params adv = {0};

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = 0x03C4;            // HID Gamepad
    fields.appearance_is_present = 1;
    fields.name = (uint8_t *)"SF900 Gamepad";
    fields.name_len = strlen("SF900 Gamepad");
    fields.name_is_complete = 1;
    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    fields.uuids16 = &hid_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) { ESP_LOGE(TAG, "adv_set_fields rc=%d (dados nao cabem em 31B?)", rc); return; }

    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv, NULL, NULL);
    ESP_LOGI(TAG, "ble_gap_adv_start rc=%d (0=ok) addr_type=%d", rc, s_own_addr_type);
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    ESP_LOGI(TAG, "nimble sync ok, addr_type=%d", s_own_addr_type);
}
static void on_reset(int reason) { ESP_LOGW(TAG, "nimble reset; reason=%d", reason); }

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_hid_init(void)
{
    if (nimble_port_init() != ESP_OK) { ESP_LOGE(TAG, "nimble_port_init falhou"); return; }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;   // Just Works (sem senha)
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("SF900 Gamepad");

    if (esp_hidd_dev_init(&s_hid_cfg, ESP_HID_TRANSPORT_BLE, hidd_cb, &s_dev) != ESP_OK)
        ESP_LOGE(TAG, "esp_hidd_dev_init falhou");

    ble_store_config_init();
    nimble_port_freertos_init(host_task);
}
