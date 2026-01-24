#ifndef JPEG_H
#define JPEG_H

#include <stdint.h>
#include "spi_ssd1306.h"
#define BITMAP_BYTES ((SPI_OLED_WIDTH * SPI_OLED_HEIGHT) / 8)

uint8_t* fetch_and_dither(const char* url);

#endif