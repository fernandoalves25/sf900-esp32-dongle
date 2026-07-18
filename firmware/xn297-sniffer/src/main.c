/*
 * Sniffer SPI passivo do XN297 do SF900 -> captura a config do TX.
 *
 * Grampeia 3 fios + GND no XN297 DENTRO do controle:
 *   controle CSN  -> GPIO10  (CS)
 *   controle SCK  -> GPIO12  (SCLK)
 *   controle DATA -> GPIO11  (MOSI do slave)   [3-wire, bidirecional]
 *   controle GND  -> GND
 * MISO do slave NAO e ligado (captura passiva, nunca dirige o barramento).
 *
 * Liga o controle DEPOIS de plugar: o init do XN297 roda no boot dele,
 * e e nesse init que saem RF_CH, endereco, data rate e calibracao.
 *
 * Decodifica os comandos e destaca os registradores que interessam.
 */
#include <stdio.h>
#include <string.h>
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_CS    10
#define PIN_SCK   12
#define PIN_DATA  11
#define MAXLEN    64

static const char* reg_name(uint8_t r)
{
    switch (r) {
        case 0x00: return "CONFIG";
        case 0x01: return "EN_AA";
        case 0x02: return "EN_RXADDR";
        case 0x03: return "SETUP_AW";
        case 0x04: return "SETUP_RETR";
        case 0x05: return "RF_CH <<<";
        case 0x06: return "RF_SETUP <<< (data rate)";
        case 0x07: return "STATUS";
        case 0x0A: return "RX_ADDR_P0 <<< (endereco)";
        case 0x0B: return "RX_ADDR_P1 <<<";
        case 0x10: return "TX_ADDR <<< (endereco)";
        case 0x11: return "RX_PW_P0";
        case 0x19: return "DEM_CAL <<< (calib)";
        case 0x1A: return "RF_CAL2 <<< (calib)";
        case 0x1B: return "DEM_CAL2 <<< (calib)";
        case 0x1C: return "DYNPD";
        case 0x1D: return "FEATURE";
        case 0x1E: return "RF_CAL <<< (calib)";
        case 0x1F: return "BB_CAL <<< (calib)";
        default:   return "";
    }
}

static void decode(const uint8_t *b, int n)
{
    if (n <= 0) return;
    uint8_t cmd = b[0];
    printf("  [%2d B] ", n);
    for (int i = 0; i < n; i++) printf("%02X ", b[i]);

    if (cmd >= 0x20 && cmd <= 0x3F) {
        uint8_t r = cmd & 0x1F;
        printf(" | W_REG %s", reg_name(r));
    } else if (cmd <= 0x1F) {
        printf(" | R_REG %s", reg_name(cmd & 0x1F));
    } else if (cmd == 0x50) {
        printf(" | ACTIVATE");
    } else if (cmd == 0x53) {
        printf(" | RST_FSPI");
    } else if (cmd == 0xA0) {
        printf(" | W_TX_PAYLOAD");
    } else if (cmd == 0xB0) {
        printf(" | W_TX_PAYLOAD_NOACK");
    } else if (cmd == 0xE1) {
        printf(" | FLUSH_TX");
    } else if (cmd == 0xE2) {
        printf(" | FLUSH_RX");
    } else if (cmd == 0xFD) {
        printf(" | CE_ON");
    } else if (cmd == 0xFC) {
        printf(" | CE_OFF");
    }
    printf("\n");
}

void app_main(void)
{
    printf("\n=== Sniffer SPI passivo do XN297 (SF900 TX) ===\n");
    printf("CS=GPIO10 SCK=GPIO12 DATA=GPIO11 (+GND). Ligue o controle agora.\n\n");

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_DATA,
        .miso_io_num = -1,          // captura passiva: nunca dirige
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_slave_interface_config_t slv = {
        .spics_io_num = PIN_CS,
        .flags = 0,
        .queue_size = 6,
        .mode = 0,                  // XN297: CPOL=0 CPHA=0
    };
    // pull-ups leves ajudam quando o barramento fica em tri-state entre transacoes
    gpio_set_pull_mode(PIN_DATA, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_SCK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_CS, GPIO_PULLUP_ONLY);

    esp_err_t ret = spi_slave_initialize(SPI2_HOST, &bus, &slv, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) { printf("ERRO spi_slave_initialize: %d\n", ret); return; }

    WORD_ALIGNED_ATTR uint8_t rxbuf[MAXLEN];
    uint32_t n_trans = 0;

    while (true) {
        memset(rxbuf, 0, sizeof(rxbuf));
        spi_slave_transaction_t t = {
            .length = MAXLEN * 8,
            .rx_buffer = rxbuf,
            .tx_buffer = NULL,
        };
        // bloqueia ate uma transacao completa (CS sobe)
        esp_err_t r = spi_slave_transmit(SPI2_HOST, &t, pdMS_TO_TICKS(2000));
        if (r == ESP_OK && t.trans_len > 0) {
            int nbytes = t.trans_len / 8;
            if (nbytes > MAXLEN) nbytes = MAXLEN;
            n_trans++;
            decode(rxbuf, nbytes);
        }
    }
}
