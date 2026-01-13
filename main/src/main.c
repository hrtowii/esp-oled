#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ssd1306.h"
#include "fonts.h" 
#include "wifi.h"
#include "esp_sntp.h"
#include "fetcher.h"
// https://github.com/NUSGreyhats/greycat2k24-badge-public/blob/main/firmware/greycat_firmware/Src/hw/oled.c#L4
static const char *TAG = "MAIN";

// https://github.com/espressif/esp-idf/blob/master/examples/system/freertos/basic_freertos_smp_usage/main/create_task_example.c
void display_task(void *arg) {
    app_state_t local_state;
    char time_buf[32];
    
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(16); // 16ms = ~60 FPS

    while (1) {
        // A. Copy Data Safely
        if (xStateMutex && xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            memcpy(&local_state, &g_app_state, sizeof(app_state_t));
            xSemaphoreGive(xStateMutex);
        }

        // B. Clear Screen
        ssd1306_Fill(Black);

        // C. Render Logic
        if (local_state.is_playing) {
            ssd1306_SetCursor(0, 0); 
            ssd1306_WriteString(local_state.song, Font_7x10, White);
            
            ssd1306_SetCursor(0, 16); 
            ssd1306_WriteString(local_state.artist, Font_7x10, White);

            // Progress Calculation
            if (local_state.start_time_ms > 0 && local_state.end_time_ms > 0) {
                int64_t now = get_current_time_ms();
                int64_t duration = local_state.end_time_ms - local_state.start_time_ms;
                int64_t progress = now - local_state.start_time_ms;

                if (progress < 0) progress = 0;
                if (progress > duration) progress = duration;

                // Percentage 0-100
                int percent = (duration > 0) ? (int)((progress * 100) / duration) : 0;
                
                // String "01:30"
                int p_sec = progress / 1000;
                snprintf(time_buf, 32, "%02d:%02d", p_sec / 60, p_sec % 60);
                
                ssd1306_SetCursor(0, 32);
                ssd1306_WriteString(time_buf, Font_7x10, White);

                // Progress Bar
                // Outline
                for (int x = 0; x < 128; x++) {
                    ssd1306_DrawPixel(x, 48, White);
                    ssd1306_DrawPixel(x, 54, White);
                }
                for (int y = 48; y <= 54; y++) {
                    ssd1306_DrawPixel(0, y, White);
                    ssd1306_DrawPixel(127, y, White);
                }
                // Fill
                int w = (128 * percent) / 100;
                for (int x = 0; x < w; x++) {
                     for (int y = 49; y < 54; y++) { // Solid fill, avoiding boundary
                         ssd1306_DrawPixel(x, y, White);
                     }
                }
            }
        } else {
             ssd1306_SetCursor(0, 0); 
             ssd1306_WriteString("No Music Playing", Font_7x10, White);
        }

        // D. Push to hardware
        ssd1306_UpdateScreen();

        // E. Precise Delay
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

    ESP_LOGI(TAG, "initing wifi+ntp");
    ESP_ERROR_CHECK(wifi_init_sta());
    start_fetcher_task();
    int task_id0 = 0;
    xTaskCreate(display_task, "display_task", 4096, (void*)task_id0, 2, NULL);
}