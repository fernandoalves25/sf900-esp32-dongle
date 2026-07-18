/* Traffic-reactive status LED on the devkit's WS2812 RGB pixel.
 *
 * Colors: dim red = wifi disconnected; dim green = connected, idle;
 * green flashes = download (wifi -> console); blue flashes = upload
 * (console -> wifi); cyan-ish = both directions at once.
 *
 * Most ESP32-S3 devkits wire the RGB LED to GPIO48 (some v1.0 boards
 * use GPIO38 - change LED_GPIO below if it stays dark).
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"

#define LED_GPIO        48
#define LED_TASK_MS     40
#define BRIGHT_MAX      70
#define BRIGHT_IDLE     4

volatile uint32_t led_traffic_rx = 0;   /* wifi -> usb (download) */
volatile uint32_t led_traffic_tx = 0;   /* usb -> wifi (upload)   */

extern bool s_wifi_is_connected;

static const char *TAG = "led_status";
static led_strip_handle_t s_strip;

static uint8_t level_from_pkts(uint32_t pkts)
{
    if (pkts == 0) {
        return 0;
    }
    uint32_t level = 10 + pkts * 4;
    return level > BRIGHT_MAX ? BRIGHT_MAX : level;
}

static void led_task(void *arg)
{
    uint32_t prev_rx = 0, prev_tx = 0;

    while (true) {
        uint32_t rx = led_traffic_rx, tx = led_traffic_tx;
        uint32_t d_rx = rx - prev_rx, d_tx = tx - prev_tx;
        prev_rx = rx;
        prev_tx = tx;

        uint8_t r = 0, g = 0, b = 0;
        if (!s_wifi_is_connected) {
            r = 8;
        } else {
            g = level_from_pkts(d_rx);
            b = level_from_pkts(d_tx);
            if (g == 0 && b == 0) {
                g = BRIGHT_IDLE;
            }
        }
        led_strip_set_pixel(s_strip, 0, r, g, b);
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(LED_TASK_MS));
    }
}

void led_status_start(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip) != ESP_OK) {
        ESP_LOGE(TAG, "RGB LED init failed (wrong GPIO %d?)", LED_GPIO);
        return;
    }
    xTaskCreate(led_task, "led_status", 2048, NULL, 3, NULL);
}
