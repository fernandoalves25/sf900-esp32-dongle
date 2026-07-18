/*
 * Receptor SF900 no modulo XN297LBW transplantado -> ESP32-S3
 *
 * Protocolo extraido do projeto axgdev/UniFrog (unifrog_input_wireless.c),
 * que fez a engenharia reversa do controle SF900/SF2000. Confere com o
 * nosso scan de RSSI (picos em 2404/2429/2449/2479 = canais 4/29/49/79).
 *
 * Sem solda no controle: so escutar com os parametros certos.
 * Saida: imprime os botoes pressionados no console (COM). Proximo passo
 * depois de confirmar: virar gamepad USB-HID.
 */
#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

#define PIN_CSN   10
#define PIN_SCK   12
#define PIN_DATA  11

static spi_device_handle_t xn;
static const uint8_t CHANNELS[] = { 0x04, 0x1d, 0x31, 0x4f };
static unsigned ch_idx = 0;

/* ---- SPI helpers (3-wire, half-duplex, cmd de 8 bits) ---- */
static void xn_reg_read(uint8_t reg, uint8_t *buf, size_t n)
{ spi_transaction_t t={.cmd=0x00|(reg&0x1F),.rx_buffer=buf,.rxlength=n*8}; ESP_ERROR_CHECK(spi_device_polling_transmit(xn,&t)); }
static uint8_t xn_r1(uint8_t reg){ uint8_t v=0; xn_reg_read(reg,&v,1); return v; }
static void xn_reg_write(uint8_t reg, const uint8_t *buf, size_t n)
{ spi_transaction_t t={.cmd=0x20|(reg&0x1F),.tx_buffer=buf,.length=n*8}; ESP_ERROR_CHECK(spi_device_polling_transmit(xn,&t)); }
static void xn_w1(uint8_t reg, uint8_t v){ xn_reg_write(reg,&v,1); }
static void xn_cmd(uint8_t cmd, uint8_t data)
{ spi_transaction_t t={.cmd=cmd,.tx_buffer=&data,.length=8}; ESP_ERROR_CHECK(spi_device_polling_transmit(xn,&t)); }
static void xn_cmd0(uint8_t cmd)
{ spi_transaction_t t={.cmd=cmd}; ESP_ERROR_CHECK(spi_device_polling_transmit(xn,&t)); }
/* le do FIFO de RX: comando 0x61 (R_RX_PAYLOAD) + n bytes */
static void xn_read_payload(uint8_t *buf, size_t n)
{ spi_transaction_t t={.cmd=0x61,.rx_buffer=buf,.rxlength=n*8}; ESP_ERROR_CHECK(spi_device_polling_transmit(xn,&t)); }

static void xn_bus_init(void)
{
    spi_bus_config_t bus={.mosi_io_num=PIN_DATA,.miso_io_num=-1,.sclk_io_num=PIN_SCK,.quadwp_io_num=-1,.quadhd_io_num=-1};
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_DISABLED));
    spi_device_interface_config_t dev={.clock_speed_hz=1000000,.mode=0,.spics_io_num=PIN_CSN,.queue_size=4,
        .flags=SPI_DEVICE_3WIRE|SPI_DEVICE_HALFDUPLEX,.command_bits=8};
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST,&dev,&xn));
}

/* replica exata do rf_stock_radio_config() do UniFrog */
static void xn_stock_config(void)
{
    static const uint8_t bb_cal[]={0x0a,0x6d,0x67,0x9c,0x46}; // 0x1F BB_CAL
    static const uint8_t rf_cal[]={0xf6,0x37,0x5d};           // 0x1E RF_CAL
    static const uint8_t addr0[]={0xdc,0xa8,0xf3,0x6b,0x74};  // 0x0A RX_ADDR_P0
    static const uint8_t addr1[]={0xb2,0x9d,0x59,0x4f,0xe3};  // 0x0B RX_ADDR_P1

    xn_cmd(0x53,0x5A); xn_cmd(0x53,0xA5); vTaskDelay(pdMS_TO_TICKS(2)); // reset
    xn_w1(0x1D,0x20);          // FEATURE = CE_SOFT
    xn_cmd(0xFC,0x00);         // CE_OFF
    xn_cmd0(0xE1); xn_cmd0(0xE2);
    xn_w1(0x07,0x70);          // limpa STATUS
    xn_reg_write(0x1F,bb_cal,sizeof(bb_cal));
    xn_reg_write(0x1E,rf_cal,sizeof(rf_cal));
    xn_w1(0x19,0x01);          // DEM_CAL: SCRAMBLE_EN=1
    xn_w1(0x00,0x8e);          // CONFIG idle
    xn_w1(0x01,0x03);          // EN_AA pipes 0,1
    xn_w1(0x02,0x03);          // EN_RXADDR pipes 0,1
    xn_w1(0x03,0x03);          // SETUP_AW = 5 bytes
    xn_w1(0x04,0x02);          // SETUP_RETR
    xn_w1(0x11,0x02);          // RX_PW_P0 = 2
    xn_w1(0x12,0x02);          // RX_PW_P1 = 2
    xn_w1(0x1C,0x00);          // DYNPD off
    xn_w1(0x06,0x3f);          // RF_SETUP
    xn_reg_write(0x0A,addr0,sizeof(addr0));
    xn_reg_write(0x0B,addr1,sizeof(addr1));
    // entra em RX
    xn_cmd(0xFC,0x00); xn_cmd0(0xE1); xn_cmd0(0xE2);
    xn_w1(0x00,0x8f);          // CONFIG = PWR_UP+PRIM_RX
    vTaskDelay(pdMS_TO_TICKS(10));
    ch_idx=0;
    xn_w1(0x05,CHANNELS[0]);   // RF_CH
    xn_cmd(0xFD,0x00);         // CE_ON
}

static void next_channel(void)
{
    ch_idx=(ch_idx+1)%(sizeof(CHANNELS));
    xn_w1(0x05,CHANNELS[ch_idx]);
}

static void print_buttons(uint32_t raw)
{
    struct { uint32_t bit; const char *n; } B[] = {
        {0x0008,"UP"},{0x0004,"DOWN"},{0x0002,"LEFT"},{0x0001,"RIGHT"},
        {0x0080,"A"},{0x0040,"B"},{0x4000,"X"},{0x2000,"Y"},
        {0x1000,"R"},{0x0800,"L"},{0x0010,"START"},{0x0020,"SELECT"},
    };
    unsigned ctrl = (raw & 0x8000) ? 2 : 1;
    printf("[ctrl %u] raw=%04X  ", ctrl, (unsigned)(raw & 0xFFFF));
    int any=0;
    for (unsigned i=0;i<sizeof(B)/sizeof(B[0]);i++)
        if (raw & B[i].bit){ printf("%s ", B[i].n); any=1; }
    if (!any) printf("(nenhum)");
    printf("\n");
}

void app_main(void)
{
    printf("\n=== Receptor SF900 (protocolo UniFrog) ===\n");
    printf("Ligue o controle e aperte botoes.\n\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    xn_bus_init();
    xn_stock_config();
    printf("STATUS inicial=0x%02X (config ok se != 00/FF)\n\n", xn_r1(0x07));

    unsigned empty=0, pkts=0;
    uint32_t last_raw=0xFFFFFFFF;

    while (true) {
        uint8_t status = xn_r1(0x07);
        if (status & 0x40) {                 // RX_DR: pacote pronto
            uint8_t pkt[2]={0,0};
            xn_cmd(0xFC,0x00);               // CE_OFF p/ ler
            xn_read_payload(pkt,2);
            xn_cmd0(0xE2);                    // FLUSH_RX
            xn_w1(0x07,0x70);                // limpa flags
            xn_cmd(0xFD,0x00);               // CE_ON
            empty=0;
            next_channel();

            uint32_t raw = ((uint32_t)pkt[0]<<8) | ((~pkt[1]) & 0xFF);
            pkts++;
            if (raw != last_raw) { print_buttons(raw); last_raw=raw; }
        } else {
            if (++empty >= 2) { empty=0; next_channel(); }
            esp_rom_delay_us(350);
        }
        // heartbeat a cada ~4000 polls sem pacote
        static uint32_t polls=0;
        if ((++polls % 20000)==0)
            printf("...escutando (pacotes ate agora: %u, ch=%d)\n", pkts, 2400+CHANNELS[ch_idx]);
    }
}
