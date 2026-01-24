// job of image fetcher:
// checks queue from fetcher.c which sends over when a new song is detected, which contains url
// http get from json.c 
// dither image to fit into 128x64 bitmap
// update draw task
#include "fetcher.h"
#include "json.h"
#include "wifi.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "spi_ssd1306.h"
#include "jpeg.h"
#define TAG "IMAGE_FETCH"
void image_task(void* arg)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(125);
    char album_art_url[128];
    while (1) {
        if (xQueueReceive(
            g_image_queue,
            album_art_url,
            portMAX_DELAY
        ) == pdTRUE ) {
            ESP_LOGI(TAG, "got image from queue: %s", &album_art_url);
            uint8_t *pixel_data = fetch_and_dither(album_art_url);
            if (pixel_data != NULL) {
                spi_oled_clear();
                spi_oled_draw_bitmap(pixel_data);
                spi_oled_update();
                // free(pixel_data);dont free and bea stupid
            }
        }
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}