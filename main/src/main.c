#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ssd1306.h"
#include "fonts.h" 
#include "wifi.h"
// https://github.com/NUSGreyhats/greycat2k24-badge-public/blob/main/firmware/greycat_firmware/Src/hw/oled.c#L4
static const char *TAG = "MAIN";

void oled_init(void) {
    if (ssd1306_Init() != 0) {
        ESP_LOGE(TAG, "OLED Init failed");
    } else {
        ESP_LOGI(TAG, "OLED Init successful");
    }
}

void oled_update_name(char *string) {
    char *string_line_1 = string;
    char *string_line_2 = "";
    
    if (strlen(string) > 11) {
        string_line_2 = string + 11;
    }

    ssd1306_Fill(Black);
    
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString("meow im ", Font_7x10, White);
    
    ssd1306_SetCursor(0, 12);
    ssd1306_WriteString(string_line_1, Font_11x18, White);
    
    ssd1306_SetCursor(0, 30);
    ssd1306_WriteString(string_line_2, Font_11x18, White);
    
    ssd1306_UpdateScreen();
}
// https://github.com/espressif/esp-idf/blob/master/examples/system/freertos/basic_freertos_smp_usage/main/create_task_example.c
void display_thread(void *arg) {
    int task_id = (int)arg;
    ESP_LOGI(TAG, "created task#%d", task_id);
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
    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    char my_name[] = "goated meow"; 
    oled_update_name(my_name);

    uint8_t brightness = 0;
    while (1) {
        // setBrightness(brightness);
        brightness = (brightness + 1) % 9;
        sprintf(my_name, "bright %d", brightness);
        oled_update_name(my_name);
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}