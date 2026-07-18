/*
 * Gamepad USB sem fio: receptor SF900 (XN297LBW transplantado) -> HID.
 *
 * Recebe os pacotes do controle SF900 pelo modulo XN297 no SPI2 e se
 * apresenta ao host (R36S / PC) como um gamepad USB-HID padrao.
 *
 * Protocolo do controle: projeto axgdev/UniFrog (ja validado aqui -
 * botoes decodificados corretamente).
 *
 * Fios do modulo XN297: CSN=GPIO10 SCK=GPIO12 DATA=GPIO11 VDD=3V3 GND=GND
 * USB: conector nativo ("USB") do DevKit.
 */
#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "ble_hid.h"
#include "led_rx.h"

/* Combinacao para trocar de modo: L + R + SELECT (segurar ~2s).
 * bits do raw: L=0x0800, R=0x1000, SELECT=0x0020 */
#define SWITCH_COMBO 0x1820u

static void switch_to_other_mode(void)
{
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next && esp_ota_set_boot_partition(next) == ESP_OK)
        printf("[MODE] trocando de modo, reiniciando...\n");
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
}

/* ================= XN297 receiver ================= */
#define PIN_CSN   10
#define PIN_SCK   12
#define PIN_DATA  11

static spi_device_handle_t xn;
static const uint8_t CHANNELS[] = { 0x04, 0x1d, 0x31, 0x4f };
static unsigned ch_idx = 0;

static void xn_reg_read(uint8_t reg, uint8_t *b, size_t n)
{ spi_transaction_t t={.cmd=0x00|(reg&0x1F),.rx_buffer=b,.rxlength=n*8}; spi_device_polling_transmit(xn,&t); }
static uint8_t xn_r1(uint8_t reg){ uint8_t v=0; xn_reg_read(reg,&v,1); return v; }
static void xn_reg_write(uint8_t reg, const uint8_t *b, size_t n)
{ spi_transaction_t t={.cmd=0x20|(reg&0x1F),.tx_buffer=b,.length=n*8}; spi_device_polling_transmit(xn,&t); }
static void xn_w1(uint8_t reg, uint8_t v){ xn_reg_write(reg,&v,1); }
static void xn_cmd(uint8_t cmd, uint8_t data)
{ spi_transaction_t t={.cmd=cmd,.tx_buffer=&data,.length=8}; spi_device_polling_transmit(xn,&t); }
static void xn_cmd0(uint8_t cmd){ spi_transaction_t t={.cmd=cmd}; spi_device_polling_transmit(xn,&t); }
static void xn_read_payload(uint8_t *b, size_t n)
{ spi_transaction_t t={.cmd=0x61,.rx_buffer=b,.rxlength=n*8}; spi_device_polling_transmit(xn,&t); }

static void xn_bus_init(void)
{
    spi_bus_config_t bus={.mosi_io_num=PIN_DATA,.miso_io_num=-1,.sclk_io_num=PIN_SCK,.quadwp_io_num=-1,.quadhd_io_num=-1};
    spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_DISABLED);
    spi_device_interface_config_t dev={.clock_speed_hz=1000000,.mode=0,.spics_io_num=PIN_CSN,.queue_size=4,
        .flags=SPI_DEVICE_3WIRE|SPI_DEVICE_HALFDUPLEX,.command_bits=8};
    spi_bus_add_device(SPI2_HOST,&dev,&xn);
}

static void xn_stock_config(void)
{
    static const uint8_t bb_cal[]={0x0a,0x6d,0x67,0x9c,0x46};
    static const uint8_t rf_cal[]={0xf6,0x37,0x5d};
    static const uint8_t addr0[]={0xdc,0xa8,0xf3,0x6b,0x74};
    static const uint8_t addr1[]={0xb2,0x9d,0x59,0x4f,0xe3};
    xn_cmd(0x53,0x5A); xn_cmd(0x53,0xA5); vTaskDelay(pdMS_TO_TICKS(2));
    xn_w1(0x1D,0x20); xn_cmd(0xFC,0x00); xn_cmd0(0xE1); xn_cmd0(0xE2); xn_w1(0x07,0x70);
    xn_reg_write(0x1F,bb_cal,sizeof(bb_cal));
    xn_reg_write(0x1E,rf_cal,sizeof(rf_cal));
    xn_w1(0x19,0x01); xn_w1(0x00,0x8e); xn_w1(0x01,0x03); xn_w1(0x02,0x03);
    xn_w1(0x03,0x03); xn_w1(0x04,0x02); xn_w1(0x11,0x02); xn_w1(0x12,0x02);
    xn_w1(0x1C,0x00); xn_w1(0x06,0x3f);
    xn_reg_write(0x0A,addr0,sizeof(addr0));
    xn_reg_write(0x0B,addr1,sizeof(addr1));
    xn_cmd(0xFC,0x00); xn_cmd0(0xE1); xn_cmd0(0xE2);
    xn_w1(0x00,0x8f); vTaskDelay(pdMS_TO_TICKS(10));
    ch_idx=0; xn_w1(0x05,CHANNELS[0]); xn_cmd(0xFD,0x00);
}
static void next_channel(void){ ch_idx=(ch_idx+1)%sizeof(CHANNELS); xn_w1(0x05,CHANNELS[ch_idx]); }

/* ================= USB HID descriptors ================= */
static const uint8_t hid_report_desc[] = { TUD_HID_REPORT_DESC_GAMEPAD() };

// mesmo report map para os dois jogadores (2 interfaces HID)
const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) { (void)instance; return hid_report_desc; }
uint16_t tud_hid_get_report_cb(uint8_t inst, uint8_t id, hid_report_type_t type, uint8_t *buf, uint16_t len)
{ (void)inst;(void)id;(void)type;(void)buf;(void)len; return 0; }
void tud_hid_set_report_cb(uint8_t inst, uint8_t id, hid_report_type_t type, const uint8_t *buf, uint16_t len)
{ (void)inst;(void)id;(void)type;(void)buf;(void)len; }

// device descriptor com VID/PID FIXOS (0x303A/0x4004 = 12346/16388).
// sem isso o esp_tinyusb calcula o PID a partir do nr de interfaces HID,
// e ele mudava (0x4004 -> 0x4008) ao ativar o 2o player, quebrando o autoconfig.
static const tusb_desc_device_t usb_device_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0x303A,
    .idProduct          = 0x4004,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

#define EPNUM_HID_P1 0x81
#define EPNUM_HID_P2 0x82
static const uint8_t usb_config_desc[] = {
    // config: 2 interfaces (Player 1 e Player 2)
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_NONE, sizeof(hid_report_desc),
                       EPNUM_HID_P1, 16, 10),
    TUD_HID_DESCRIPTOR(1, 5, HID_ITF_PROTOCOL_NONE, sizeof(hid_report_desc),
                       EPNUM_HID_P2, 16, 10),
};

static const char *usb_strings[] = {
    (char[]){0x09, 0x04},   // 0: idioma (EN)
    "DataFrog-Transplant",  // 1: fabricante
    "SF900 Wireless Gamepad", // 2: produto
    "SF900-001",            // 3: serial
    "SF900 Player 1",       // 4: interface HID P1
    "SF900 Player 2",       // 5: interface HID P2
};

/* ================= button mapping ================= */
static uint8_t dpad_to_hat(uint32_t raw)
{
    int up=raw&0x0008, dn=raw&0x0004, lf=raw&0x0002, rt=raw&0x0001;
    if (up&&rt) return GAMEPAD_HAT_UP_RIGHT;
    if (rt&&dn) return GAMEPAD_HAT_DOWN_RIGHT;
    if (dn&&lf) return GAMEPAD_HAT_DOWN_LEFT;
    if (lf&&up) return GAMEPAD_HAT_UP_LEFT;
    if (up) return GAMEPAD_HAT_UP;
    if (rt) return GAMEPAD_HAT_RIGHT;
    if (dn) return GAMEPAD_HAT_DOWN;
    if (lf) return GAMEPAD_HAT_LEFT;
    return GAMEPAD_HAT_CENTERED;
}
static uint32_t map_buttons(uint32_t raw)
{
    uint32_t b=0;
    if (raw&0x0080) b|=GAMEPAD_BUTTON_0; // A
    if (raw&0x0040) b|=GAMEPAD_BUTTON_1; // B
    if (raw&0x4000) b|=GAMEPAD_BUTTON_2; // X
    if (raw&0x2000) b|=GAMEPAD_BUTTON_3; // Y
    if (raw&0x0800) b|=GAMEPAD_BUTTON_4; // L
    if (raw&0x1000) b|=GAMEPAD_BUTTON_5; // R
    if (raw&0x0020) b|=GAMEPAD_BUTTON_6; // SELECT
    if (raw&0x0010) b|=GAMEPAD_BUTTON_7; // START
    // D-pad como BOTOES 8-11 (o RetroArch do R36S/ArkOS le o D-pad assim,
    // igual ao controle interno; o hat nao funciona nesse build)
    if (raw&0x0008) b|=GAMEPAD_BUTTON_8;  // UP
    if (raw&0x0004) b|=GAMEPAD_BUTTON_9;  // DOWN
    if (raw&0x0002) b|=GAMEPAD_BUTTON_10; // LEFT
    if (raw&0x0001) b|=GAMEPAD_BUTTON_11; // RIGHT
    return b;
}

// envia para a interface HID do jogador (itf 0 = P1, itf 1 = P2)
static void send_report(uint8_t itf, uint32_t raw)
{
    if (!tud_hid_n_ready(itf)) return;
    hid_gamepad_report_t r = {0};
    r.hat = dpad_to_hat(raw);
    r.buttons = map_buttons(raw);
    tud_hid_n_report(itf, 0, &r, sizeof(r));
}

void app_main(void)
{
    printf("\n=== SF900 wireless gamepad (USB-HID) ===\n");
    printf("Trocar p/ modo WiFi: segure L+R+SELECT por 2s.\n");

    // USB
    tinyusb_config_t tcfg = {
        .device_descriptor = &usb_device_desc,   // VID/PID fixos (0x303A/0x4004)
        .string_descriptor = usb_strings,
        .string_descriptor_count = sizeof(usb_strings)/sizeof(usb_strings[0]),
        .external_phy = false,
        .configuration_descriptor = usb_config_desc,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tcfg));

    // BLE HID (roda junto com o USB) — precisa de NVS p/ bonding
    esp_err_t nv = nvs_flash_init();
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }
    ble_hid_init();
    printf("BLE: pareie com 'SF900 Gamepad'.\n");

    // radio
    xn_bus_init();
    xn_stock_config();
    led_rx_start();   // LED verde fraco pisca a cada pacote recebido
    printf("XN297 STATUS=0x%02X, aguardando controle...\n", xn_r1(0x07));

    unsigned empty=0;
    uint32_t last[2]={0xFFFFFFFF,0xFFFFFFFF};   // por jogador
    TickType_t combo_start=0, combo_last_seen=0;
    while (true) {
        uint8_t status = xn_r1(0x07);
        if (status & 0x40) {
            uint8_t pkt[2]={0,0};
            xn_cmd(0xFC,0x00);
            xn_read_payload(pkt,2);
            xn_cmd0(0xE2); xn_w1(0x07,0x70); xn_cmd(0xFD,0x00);
            empty=0; next_channel();
            led_rx_count++;   // pisca o LED verde (pacote recebido)
            uint32_t raw = ((uint32_t)pkt[0]<<8) | ((~pkt[1]) & 0xFF);

            // qual jogador? pipe do STATUS (bits 3:1); fallback no bit 0x8000
            unsigned pipe = (status >> 1) & 0x07;
            unsigned port = (pipe == 0) ? 0 : (pipe == 1) ? 1 : ((raw & 0x8000) ? 1 : 0);
            if (raw != last[port]) {
                send_report(port, raw);            // P1->itf0, P2->itf1
                if (port == 0) ble_hid_update(raw); // BLE espelha o Player 1
                last[port] = raw;
            }

            TickType_t now = xTaskGetTickCount();
            if ((raw & SWITCH_COMBO) == SWITCH_COMBO) {
                if (combo_start == 0) combo_start = now;
                combo_last_seen = now;
            } else {
                combo_start = 0;   // soltou algum botao do combo
            }
        } else {
            if (++empty >= 2) { empty=0; next_channel(); }
            esp_rom_delay_us(300);
        }
        // checa duracao do combo TODA iteracao (mesmo sem novos pacotes)
        if (combo_start != 0) {
            TickType_t now = xTaskGetTickCount();
            if ((now - combo_last_seen) > pdMS_TO_TICKS(400)) {
                combo_start = 0;   // pacotes do combo pararam -> soltou
            } else if ((now - combo_start) > pdMS_TO_TICKS(1500)) {
                printf("[MODE] L+R+SELECT -> trocando para WiFi...\n");
                switch_to_other_mode();
            }
        }
    }
}
