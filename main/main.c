#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ssd1306.h"
#include "fonts.h" 
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


void app_main(void) {

    oled_init();

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