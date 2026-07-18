/* LED RGB do devkit pisca verde (baixa luminosidade) a cada pacote recebido
 * do controle SF900. GPIO48 na maioria dos ESP32-S3 (alguns v1.0 usam 38). */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"
#include "led_rx.h"

#define LED_GPIO     48
#define TICK_MS      25
#define GREEN_LOW    8    /* luminosidade baixa */

volatile uint32_t led_rx_count = 0;   /* incrementado a cada pacote recebido */

static led_strip_handle_t s_strip;

static void led_task(void *arg)
{
    uint32_t prev = 0;
    while (true) {
        uint32_t now = led_rx_count;
        uint8_t g = (now != prev) ? GREEN_LOW : 0;   /* verde no pacote, apaga se parado */
        prev = now;
        led_strip_set_pixel(s_strip, 0, 0, g, 0);
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

void led_rx_start(void)
{
    led_strip_config_t strip_cfg = { .strip_gpio_num = LED_GPIO, .max_leds = 1 };
    led_strip_rmt_config_t rmt_cfg = { .resolution_hz = 10 * 1000 * 1000 };
    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip) != ESP_OK) {
        ESP_LOGE("led_rx", "RGB LED init falhou (GPIO %d errado?)", LED_GPIO);
        return;
    }
    xTaskCreate(led_task, "led_rx", 2048, NULL, 3, NULL);
}
