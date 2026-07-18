/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_hosted.h"
#include "esp_hosted_misc.h"
#endif
#include "esp_mac.h"
#include "sdkconfig.h"

#include "cmd_wifi.h"

#include "driver/spi_master.h"
#include "esp_ota_ops.h"
#include "esp_rom_sys.h"

#include "tinyusb.h"
#include "tinyusb_net.h"
#include "tusb_console.h"
#include "tusb_bth.h"

#if CFG_TUD_CDC
#include "tusb_cdc_acm.h"
static uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];
void Command_Parse(char* Cmd);
tinyusb_config_cdcacm_t amc_cfg;
#elif CONFIG_UART_ENABLE
void initialise_uart(void);
#endif

#ifdef CONFIG_HEAP_TRACING
#include "esp_heap_trace.h"
#define NUM_RECORDS 100
static heap_trace_record_t trace_record[NUM_RECORDS]; // This buffer must be in internal RAM
#endif /* CONFIG_HEAP_TRACING */

static const char *TAG = "USB_Dongle";

/*
 * Register commands that can be used with FreeRTOS+CLI through the UDP socket.
 * The commands are defined in CLI-commands.c.
 */
void vRegisterCLICommands(void);

#if CFG_TUD_CDC
void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    /* initialization */
    size_t rx_size = 0;

    /* read */
    esp_err_t ret = tinyusb_cdcacm_read(0, buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    if (ret == ESP_OK) {
        buf[rx_size] = '\0';
        Command_Parse((char*)buf);
    } else {
        ESP_LOGE(TAG, "itf %d: itf Read error", itf);
    }
}

void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
    int dtr = event->line_state_changed_data.dtr;
    int rst = event->line_state_changed_data.rts;
    ESP_LOGI(TAG, "Line state changed! itf:%d dtr:%d, rst:%d", itf, dtr, rst);
}
#endif /* CFG_TUD_CDC */

/* ===== vigia leve do controle SF900 para trocar de modo =====
 * Escuta o XN297 (SPI2) em background; se L+R+SELECT for segurado ~2s,
 * reinicia na slot OTA do gamepad. Poll folgado para nao atrapalhar o WiFi.
 * bits do raw: L=0x0800, R=0x1000, SELECT=0x0020 -> combo 0x1820 */
#define SW_CSN 10
#define SW_SCK 12
#define SW_DATA 11
#define SW_COMBO 0x1820u
static const uint8_t SW_CH[] = { 0x04, 0x1d, 0x31, 0x4f };

static spi_device_handle_t sw_xn;
static void sw_rr(uint8_t r, uint8_t *b, size_t n){ spi_transaction_t t={.cmd=0x00|(r&0x1F),.rx_buffer=b,.rxlength=n*8}; spi_device_polling_transmit(sw_xn,&t); }
static uint8_t sw_r1(uint8_t r){ uint8_t v=0; sw_rr(r,&v,1); return v; }
static void sw_wr(uint8_t r, const uint8_t *b, size_t n){ spi_transaction_t t={.cmd=0x20|(r&0x1F),.tx_buffer=b,.length=n*8}; spi_device_polling_transmit(sw_xn,&t); }
static void sw_w1(uint8_t r, uint8_t v){ sw_wr(r,&v,1); }
static void sw_cmd(uint8_t c, uint8_t d){ spi_transaction_t t={.cmd=c,.tx_buffer=&d,.length=8}; spi_device_polling_transmit(sw_xn,&t); }
static void sw_cmd0(uint8_t c){ spi_transaction_t t={.cmd=c}; spi_device_polling_transmit(sw_xn,&t); }
static void sw_rpl(uint8_t *b, size_t n){ spi_transaction_t t={.cmd=0x61,.rx_buffer=b,.rxlength=n*8}; spi_device_polling_transmit(sw_xn,&t); }

static void xn297_switch_watcher(void *arg)
{
    spi_bus_config_t bus={.mosi_io_num=SW_DATA,.miso_io_num=-1,.sclk_io_num=SW_SCK,.quadwp_io_num=-1,.quadhd_io_num=-1};
    if (spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_DISABLED)!=ESP_OK) vTaskDelete(NULL);
    spi_device_interface_config_t dev={.clock_speed_hz=1000000,.mode=0,.spics_io_num=SW_CSN,.queue_size=4,
        .flags=SPI_DEVICE_3WIRE|SPI_DEVICE_HALFDUPLEX,.command_bits=8};
    spi_bus_add_device(SPI2_HOST,&dev,&sw_xn);

    static const uint8_t bb[]={0x0a,0x6d,0x67,0x9c,0x46}, rc[]={0xf6,0x37,0x5d};
    static const uint8_t a0[]={0xdc,0xa8,0xf3,0x6b,0x74}, a1[]={0xb2,0x9d,0x59,0x4f,0xe3};
    sw_cmd(0x53,0x5A); sw_cmd(0x53,0xA5); vTaskDelay(pdMS_TO_TICKS(2));
    sw_w1(0x1D,0x20); sw_cmd(0xFC,0x00); sw_cmd0(0xE1); sw_cmd0(0xE2); sw_w1(0x07,0x70);
    sw_wr(0x1F,bb,sizeof(bb)); sw_wr(0x1E,rc,sizeof(rc));
    sw_w1(0x19,0x01); sw_w1(0x00,0x8e); sw_w1(0x01,0x03); sw_w1(0x02,0x03);
    sw_w1(0x03,0x03); sw_w1(0x04,0x02); sw_w1(0x11,0x02); sw_w1(0x12,0x02);
    sw_w1(0x1C,0x00); sw_w1(0x06,0x3f); sw_wr(0x0A,a0,sizeof(a0)); sw_wr(0x0B,a1,sizeof(a1));
    sw_cmd(0xFC,0x00); sw_cmd0(0xE1); sw_cmd0(0xE2); sw_w1(0x00,0x8f); vTaskDelay(pdMS_TO_TICKS(10));
    unsigned ci=0; sw_w1(0x05,SW_CH[0]); sw_cmd(0xFD,0x00);

    TickType_t combo_start=0, combo_last=0; unsigned empty=0;
    while (1) {
        if (sw_r1(0x07) & 0x40) {
            uint8_t pkt[2]={0,0};
            sw_cmd(0xFC,0x00); sw_rpl(pkt,2); sw_cmd0(0xE2); sw_w1(0x07,0x70); sw_cmd(0xFD,0x00);
            empty=0; ci=(ci+1)%sizeof(SW_CH); sw_w1(0x05,SW_CH[ci]);
            uint32_t raw=((uint32_t)pkt[0]<<8)|((~pkt[1])&0xFF);
            TickType_t now=xTaskGetTickCount();
            if ((raw & SW_COMBO)==SW_COMBO) { if (combo_start==0) combo_start=now; combo_last=now; }
            else combo_start=0;
        } else {
            if (++empty>=2){ empty=0; ci=(ci+1)%sizeof(SW_CH); sw_w1(0x05,SW_CH[ci]); }
        }
        if (combo_start != 0) {
            TickType_t now=xTaskGetTickCount();
            if ((now-combo_last)>pdMS_TO_TICKS(400)) combo_start=0;
            else if ((now-combo_start)>pdMS_TO_TICKS(1500)) {
                const esp_partition_t *next=esp_ota_get_next_update_partition(NULL);
                if (next) esp_ota_set_boot_partition(next);
                vTaskDelay(pdMS_TO_TICKS(100)); esp_restart();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));   // folga para o WiFi
    }
}

void app_main(void)
{
    xTaskCreate(xn297_switch_watcher, "mode_sw", 4096, NULL, 4, NULL);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#if CFG_TUD_NCM || CFG_TUD_ECM_RNDIS
    // Get MAC address BEFORE USB initialization, so it's set when USB enumerates
    // This is critical if USB is connected before power-on
    uint8_t mac_addr[6] = {0};
#if CONFIG_IDF_TARGET_ESP32P4
    esp_hosted_connect_to_slave();
    esp_err_t mac_ret = esp_hosted_iface_mac_addr_get(mac_addr, 6, ESP_MAC_WIFI_STA);
    if (mac_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get slave STA MAC address: %s, using default", esp_err_to_name(mac_ret));
    } else {
        // Update TinyUSB RNDIS MAC address BEFORE USB initialization
        memcpy(tud_network_mac_address, mac_addr, 6);
        ESP_LOGI(TAG, "Slave STA MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
    }
#else
    esp_read_mac(mac_addr, ESP_MAC_WIFI_STA);
    // Update TinyUSB RNDIS MAC address BEFORE USB initialization
    memcpy(tud_network_mac_address, mac_addr, 6);
    ESP_LOGI(TAG, "USB Network MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);
#endif
#endif /* CFG_TUD_NCM || CFG_TUD_ECM_RNDIS */

    ESP_LOGI(TAG, "USB initialization");

    tinyusb_config_t tusb_cfg = {
        .external_phy = false // In the most cases you need to use a `false` value
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

#if CFG_TUD_NCM || CFG_TUD_ECM_RNDIS
    tinyusb_net_config_t tusb_net_cfg = {
        .on_recv_callback = wifi_recv_callback,
        .free_tx_buffer = wifi_buffer_free,
    };
    memcpy(tusb_net_cfg.mac_addr, mac_addr, 6);

    tinyusb_net_init(TINYUSB_USBDEV_0, &tusb_net_cfg);
    initialise_wifi();
#endif /* CFG_TUD_NCM || CFG_TUD_ECM_RNDIS */

#if CFG_TUD_BTH
    // init ble controller
    tusb_bth_init();
#endif /* CFG_TUD_BTH */

#if CFG_TUD_CDC
    tinyusb_config_cdcacm_t amc_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 128,
        .callback_rx = &tinyusb_cdc_rx_callback, // the first way to register a callback
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = &tinyusb_cdc_line_state_changed_callback,
        .callback_line_coding_changed = NULL
    };

    ESP_ERROR_CHECK(tusb_cdc_acm_init(&amc_cfg));
    esp_tusb_init_console(TINYUSB_CDC_ACM_0);
#elif CONFIG_UART_ENABLE
    initialise_uart();
#endif /* CFG_TUD_CDC */

    ESP_LOGI(TAG, "USB initialization DONE");

#ifdef CONFIG_HEAP_TRACING
    heap_trace_init_standalone(trace_record, NUM_RECORDS);
    heap_trace_start(HEAP_TRACE_LEAKS);
#endif

    /* Register commands with the FreeRTOS+CLI command interpreter. */
    vRegisterCLICommands();
}
