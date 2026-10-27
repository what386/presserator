#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb.h"
#include "bluetooth.h"

#ifndef BLINKY_STATUS_LED_GPIO
#define BLINKY_STATUS_LED_GPIO GPIO_NUM_2
#endif

#ifndef BLINKY_STATUS_LED_ACTIVE_LEVEL
#define BLINKY_STATUS_LED_ACTIVE_LEVEL 1
#endif

namespace {

constexpr char kTag[] = "blinky";
constexpr gpio_num_t kStatusLed = BLINKY_STATUS_LED_GPIO;
constexpr int kOnLevel = BLINKY_STATUS_LED_ACTIVE_LEVEL;
constexpr int kOffLevel = kOnLevel == 0 ? 1 : 0;
constexpr TickType_t kBlinkInterval = pdMS_TO_TICKS(500);

} // namespace

extern "C" void app_main() {
    ESP_ERROR_CHECK(usb_init());
    ESP_ERROR_CHECK(bluetooth_init());

    ESP_LOGI(kTag, "Blinking status LED on GPIO %d", static_cast<int>(kStatusLed));

    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << static_cast<unsigned int>(kStatusLed);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(kStatusLed, kOffLevel));

    while (true) {
        ESP_ERROR_CHECK(gpio_set_level(kStatusLed, kOnLevel));
        vTaskDelay(kBlinkInterval);
        ESP_ERROR_CHECK(gpio_set_level(kStatusLed, kOffLevel));
        vTaskDelay(kBlinkInterval);
    }
}
