#include "spi_ssd1306.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SPI_OLED";
static spi_device_handle_t spi_handle;
static uint8_t spi_oled_buffer[SPI_BUFFER_SIZE];

static void oled_send_cmd(uint8_t cmd) {
    gpio_set_level(OLED_SPI_DC, 0); // DC Low = Command
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(spi_handle, &t);
}

static void oled_send_data(const uint8_t *data, size_t len) {
    gpio_set_level(OLED_SPI_DC, 1); // DC High = Data
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(spi_handle, &t);
}

void spi_oled_init(void) {
    gpio_set_direction(OLED_SPI_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(OLED_SPI_RES, GPIO_MODE_OUTPUT);
    
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = OLED_SPI_MOSI,
        .sclk_io_num = OLED_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_BUFFER_SIZE
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = OLED_SPI_CS,
        .queue_size = 7,
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle);

    gpio_set_level(OLED_SPI_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(OLED_SPI_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    oled_send_cmd(0xAE); // Display Off
    oled_send_cmd(0xD5); // Set Clock Divide Ratio
    oled_send_cmd(0x80);
    oled_send_cmd(0xA8); // Set Multiplex Ratio
    oled_send_cmd(0x3F);
    oled_send_cmd(0xD3); // Set Display Offset
    oled_send_cmd(0x00);
    oled_send_cmd(0x40); // Set Start Line
    oled_send_cmd(0x8D); // Charge Pump
    oled_send_cmd(0x14);
    oled_send_cmd(0x20); // Memory Addressing Mode
    oled_send_cmd(0x00); // 0x00 = Horizontal Mode (Important!)
    oled_send_cmd(0xA1); // Segment Re-map
    oled_send_cmd(0xC8); // COM Output Scan Direction
    oled_send_cmd(0xDA); // COM Pins hardware config
    oled_send_cmd(0x12);
    oled_send_cmd(0x81); // Contrast
    oled_send_cmd(0xCF);
    oled_send_cmd(0xD9); // Pre-charge Period
    oled_send_cmd(0xF1);
    oled_send_cmd(0xDB); // VCOMH Deselect Level
    oled_send_cmd(0x40);
    oled_send_cmd(0xA4); // Entire Display ON (Resume)
    oled_send_cmd(0xA6); // Normal Display
    oled_send_cmd(0xAF); // Display ON
    
    ESP_LOGI(TAG, "SPI OLED Initialized");
}

void spi_oled_clear(void) {
    memset(spi_oled_buffer, 0, SPI_BUFFER_SIZE);
}

void spi_oled_update(void) {
    oled_send_cmd(0x21); 
    oled_send_cmd(0);   // Start
    oled_send_cmd(127); // End
    oled_send_cmd(0x22);
    oled_send_cmd(0);   // Start
    oled_send_cmd(7);   // End (8 pages)

    oled_send_data(spi_oled_buffer, SPI_BUFFER_SIZE);
}

void spi_oled_draw_bitmap(const uint8_t *bitmap) {
    memcpy(spi_oled_buffer, bitmap, SPI_BUFFER_SIZE);
}

void spi_oled_draw_pixel(int x, int y, uint8_t color) {
    if (x >= 0 && x < 128 && y >= 0 && y < 64) {
        if (color) spi_oled_buffer[x + (y / 8) * 128] |= (1 << (y % 8));
        else       spi_oled_buffer[x + (y / 8) * 128] &= ~(1 << (y % 8));
    }
}