#ifndef SPI_SSD1306_H
#define SPI_SSD1306_H

#include <stdint.h>
#include "driver/spi_master.h"

#define OLED_SPI_SCK  7
#define OLED_SPI_MOSI 8
#define OLED_SPI_CS   6
#define OLED_SPI_DC   9
#define OLED_SPI_RES  10

#define SPI_OLED_WIDTH  128
#define SPI_OLED_HEIGHT 64
#define SPI_BUFFER_SIZE (SPI_OLED_WIDTH * SPI_OLED_HEIGHT / 8)

void spi_oled_init(void);

void spi_oled_clear(void);

void spi_oled_update(void);

void spi_oled_draw_bitmap(const uint8_t *bitmap);

void spi_oled_draw_pixel(int x, int y, uint8_t color);

#endif