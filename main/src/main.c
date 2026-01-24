#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "ssd1306.h"
#include "fonts.h" 
#include "wifi.h"
#include "fetcher.h"
#include "spi_ssd1306.h"
#include "image_task.h"
// https://github.com/NUSGreyhats/greycat2k24-badge-public/blob/main/firmware/greycat_firmware/Src/hw/oled.c#L4
static const char *TAG = "MAIN";

// https://github.com/espressif/esp-idf/blob/master/examples/system/freertos/basic_freertos_smp_usage/main/create_task_example.c
void display_task(void *arg) {
    app_state_t local_state;
    char time_buf[32];
    static MarqueeState_t song_marquee = {0, 0, ""};
    static MarqueeState_t artist_marquee = {0, 0, ""};
    
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(16); // 16ms = ~60 FPS

    while (1) {
        if (xStateMutex && xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            memcpy(&local_state, &g_app_state, sizeof(app_state_t));
            xSemaphoreGive(xStateMutex);
        }

        ssd1306_Fill(Black);

        if (local_state.is_playing) {
            ssd1306_SetCursor(0, 0); 
            ssd1306_WriteStringMarquee(local_state.song, Font_7x10, White,
                                        128, &song_marquee, 
                                        MARQUEE_SPEED, MARQUEE_START_DELAY, MARQUEE_LOOP_GAP);
            
            ssd1306_SetCursor(0, 16); 
            ssd1306_WriteStringMarquee(local_state.artist, Font_7x10, White,
                                        128, &artist_marquee,
                                        MARQUEE_SPEED, MARQUEE_START_DELAY, MARQUEE_LOOP_GAP);

            if (local_state.start_time_ms > 0 && local_state.end_time_ms > 0) {
                int64_t now = get_current_time_ms();
                int64_t duration = local_state.end_time_ms - local_state.start_time_ms;
                int64_t progress = now - local_state.start_time_ms;

                if (progress < 0) progress = 0;
                if (progress > duration) progress = duration;

                int percent = (duration > 0) ? (int)((progress * 100) / duration) : 0;
                
                int p_sec = progress / 1000;
                snprintf(time_buf, 32, "%02d:%02d", p_sec / 60, p_sec % 60);
                
                ssd1306_SetCursor(0, 32);
                ssd1306_WriteString(time_buf, Font_7x10, White);


                for (int x = 0; x < 128; x++) {
                    ssd1306_DrawPixel(x, 48, White);
                    ssd1306_DrawPixel(x, 54, White);
                }
                for (int y = 48; y <= 54; y++) {
                    ssd1306_DrawPixel(0, y, White);
                    ssd1306_DrawPixel(127, y, White);
                }
                int w = (128 * percent) / 100;
                for (int x = 0; x < w; x++) {
                     for (int y = 49; y < 54; y++) {
                         ssd1306_DrawPixel(x, y, White);
                     }
                }
            }
        } else {
             ssd1306_SetCursor(0, 0); 
             ssd1306_WriteString("No Music Playing", Font_7x10, White);
        }

        ssd1306_UpdateScreen();

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    // nvs needed fr wifi
    ESP_ERROR_CHECK(ret);
    oled_init();
    spi_oled_init();
    spi_oled_clear();
    ESP_LOGI(TAG, "initing wifi");
    ESP_ERROR_CHECK(wifi_init_sta());

    ESP_LOGI(TAG, "syncing NTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("0.sg.pool.ntp.org");
    esp_netif_sntp_init(&config);
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) == ESP_ERR_TIMEOUT) {
        ESP_LOGI(TAG, "Waiting for SNTP to sync...");
    }
    // ESP_LOGI(TAG, "time synced");
    // time_t rawtime;
    // struct tm * timeinfo;
    // time(&rawtime);
    // timeinfo = localtime(&rawtime);
    // char output[100];
    // sprintf(output, "[%d/%d/%d %d:%d:%d]", timeinfo->tm_mday,
    //         timeinfo->tm_mon + 1, timeinfo->tm_year + 1900,
    //         timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    // ESP_LOGI(TAG, "%s", output);

    start_fetcher_task();
    int task_id0 = 0;
    xTaskCreate(display_task, "display_task", 4096, (void*)task_id0, 2, NULL);
    int task_id1 = 0;
    xTaskCreate(image_task, "image_task", 4096, (void*)task_id1, 2, NULL);
}