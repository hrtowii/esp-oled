#include <string.h>
#include "ssd1306.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// code stolen from ssd1306 hal written for STM32, just ripped out handle -> changed to i2c driver code
static const char *TAG = "DISPLAY: ";

#define I2C_MASTER_SCL_IO           4
#define I2C_MASTER_SDA_IO           5
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          400000
#define I2C_MASTER_TX_BUF_DISABLE   0
#define I2C_MASTER_RX_BUF_DISABLE   0
#define I2C_MASTER_TIMEOUT_MS       1000

#ifndef SSD1306_I2C_ADDR
#define SSD1306_I2C_ADDR 0x3C
#endif

static uint8_t _i2c_addr = (SSD1306_I2C_ADDR > 0x70) ? (SSD1306_I2C_ADDR >> 1) : SSD1306_I2C_ADDR;

static uint8_t SSD1306_Buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

static SSD1306_t SSD1306;

void ssd1306_I2C_Init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        // .clk_flags = 0, 
    };
    
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}


static uint8_t ssd1306_WriteCommand(uint8_t command)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    i2c_master_start(cmd);
    // Write Address + Write Bit
    i2c_master_write_byte(cmd, (_i2c_addr << 1) | I2C_MASTER_WRITE, true);
    // Control Byte: 0x00 indicates that the next byte is a command
    i2c_master_write_byte(cmd, 0x00, true);
    // The Command
    i2c_master_write_byte(cmd, command, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    return (ret == ESP_OK) ? 0 : 1;
}


//
//  Initialize the oled screen
//
uint8_t ssd1306_Init(void)
{
    // Initialize I2C Hardware first
    ssd1306_I2C_Init();

    // Wait for the screen to boot
    vTaskDelay(pdMS_TO_TICKS(100)); // Replcaed HAL_Delay
    int status = 0;

    // Init LCD
    status += ssd1306_WriteCommand(0xAE);   // Display off
    status += ssd1306_WriteCommand(0x20);   // Set Memory Addressing Mode
    status += ssd1306_WriteCommand(0x10);   // 00,Horizontal Addressing Mode;01,Vertical Addressing Mode;10,Page Addressing Mode (RESET);11,Invalid
    status += ssd1306_WriteCommand(0xB0);   // Set Page Start Address for Page Addressing Mode,0-7
    status += ssd1306_WriteCommand(0xC8);   // Set COM Output Scan Direction
    status += ssd1306_WriteCommand(0x00);   // Set low column address
    status += ssd1306_WriteCommand(0x10);   // Set high column address
    status += ssd1306_WriteCommand(0x40);   // Set start line address
    status += ssd1306_WriteCommand(0x81);   // set contrast control register
    status += ssd1306_WriteCommand(0xFF);
    status += ssd1306_WriteCommand(0xA1);   // Set segment re-map 0 to 127
    status += ssd1306_WriteCommand(0xA6);   // Set normal display

    status += ssd1306_WriteCommand(0xA8);   // Set multiplex ratio(1 to 64)
    status += ssd1306_WriteCommand(SSD1306_HEIGHT - 1);

    status += ssd1306_WriteCommand(0xA4);   // 0xa4,Output follows RAM content;0xa5,Output ignores RAM content
    status += ssd1306_WriteCommand(0xD3);   // Set display offset
    status += ssd1306_WriteCommand(0x00);   // No offset
    status += ssd1306_WriteCommand(0xD5);   // Set display clock divide ratio/oscillator frequency
    status += ssd1306_WriteCommand(0xF0);   // Set divide ratio
    status += ssd1306_WriteCommand(0xD9);   // Set pre-charge period
    status += ssd1306_WriteCommand(0x22);

    status += ssd1306_WriteCommand(0xDA);   // Set com pins hardware configuration
    status += ssd1306_WriteCommand(SSD1306_COM_LR_REMAP << 5 | SSD1306_COM_ALTERNATIVE_PIN_CONFIG << 4 | 0x02);   

    status += ssd1306_WriteCommand(0xDB);   // Set vcomh
    status += ssd1306_WriteCommand(0x20);   // 0x20,0.77xVcc
    status += ssd1306_WriteCommand(0x8D);   // Set DC-DC enable
    status += ssd1306_WriteCommand(0x14);   //
    status += ssd1306_WriteCommand(0xAF);   // Turn on SSD1306 panel

    if (status != 0) {
        return 1;
    }

    // Clear screen
    ssd1306_Fill(Black);

    // Flush buffer to screen
    ssd1306_UpdateScreen();

    // Set default values for screen object
    SSD1306.CurrentX = 0;
    SSD1306.CurrentY = 0;

    SSD1306.Initialized = 1;

    return 0;
}

//
//  Fill the whole screen with the given color
//
void ssd1306_Fill(SSD1306_COLOR color)
{
    // Fill screenbuffer with a constant value (color)
    uint32_t i;

    for(i = 0; i < sizeof(SSD1306_Buffer); i++)
    {
        SSD1306_Buffer[i] = (color == Black) ? 0x00 : 0xFF;
    }
}

//
//  Write the screenbuffer with changed to the screen
//
void ssd1306_UpdateScreen(void)
{
    uint8_t i;

    for (i = 0; i < 8; i++) {
        // Send page setup commands
        ssd1306_WriteCommand(0xB0 + i);
        ssd1306_WriteCommand(0x00);
        ssd1306_WriteCommand(0x10);

        // Send data for this page
        // Original HAL: HAL_I2C_Mem_Write(hi2c, SSD1306_I2C_ADDR, 0x40, 1, &SSD1306_Buffer[SSD1306_WIDTH * i], SSD1306_WIDTH, 100);
        
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        // Addr + Write
        i2c_master_write_byte(cmd, (_i2c_addr << 1) | I2C_MASTER_WRITE, true);
        // Control Byte 0x40 indicates next bytes are DATA (stored in RAM)
        i2c_master_write_byte(cmd, 0x40, true); 
        // Write the chunk of buffer
        i2c_master_write(cmd, &SSD1306_Buffer[SSD1306_WIDTH * i], SSD1306_WIDTH, true);
        i2c_master_stop(cmd);

        i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
    }
}

//
//  Draw one pixel in the screenbuffer
//  X => X Coordinate
//  Y => Y Coordinate
//  color => Pixel color
//
void ssd1306_DrawPixel(uint8_t x, uint8_t y, SSD1306_COLOR color)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT)
    {
        // Don't write outside the buffer
        return;
    }

    // Check if pixel should be inverted
    if (SSD1306.Inverted)
    {
        color = (SSD1306_COLOR)!color;
    }

    // Draw in the correct color
    if (color == White)
    {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] |= 1 << (y % 8);
    }
    else
    {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
    }
}


//
//  Draw 1 char to the screen buffer
//  ch      => Character to write
//  Font    => Font to use
//  color   => Black or White
//
char ssd1306_WriteChar(char ch, FontDef Font, SSD1306_COLOR color)
{
    uint32_t i, b, j;

    // Check remaining space on current line
    if (SSD1306_WIDTH <= (SSD1306.CurrentX + Font.FontWidth) ||
        SSD1306_HEIGHT <= (SSD1306.CurrentY + Font.FontHeight))
    {
        // Not enough space on current line
        return 0;
    }

    // Translate font to screenbuffer
    for (i = 0; i < Font.FontHeight; i++)
    {
        b = Font.data[(ch - 32) * Font.FontHeight + i];
        for (j = 0; j < Font.FontWidth; j++)
        {
            if ((b << j) & 0x8000)
            {
                ssd1306_DrawPixel(SSD1306.CurrentX + j, (SSD1306.CurrentY + i), (SSD1306_COLOR) color);
            }
            else
            {
                ssd1306_DrawPixel(SSD1306.CurrentX + j, (SSD1306.CurrentY + i), (SSD1306_COLOR)!color);
            }
        }
    }

    // The current space is now taken
    SSD1306.CurrentX += Font.FontWidth;

    // Return written char for validation
    return ch;
}

//
//  Write full string to screenbuffer
//
char ssd1306_WriteString(const char* str, FontDef Font, SSD1306_COLOR color)
{
    // Write until null-byte
    while (*str)
    {
        if (ssd1306_WriteChar(*str, Font, color) != *str)
        {
            // Char could not be written
            return *str;
        }

        // Next char
        str++;
    }

    // Everything ok
    return *str;
}

//
//  Invert background/foreground colors
//
void ssd1306_InvertColors(void)
{
    SSD1306.Inverted = !SSD1306.Inverted;
}

//
//  Set cursor position
//
void ssd1306_SetCursor(uint8_t x, uint8_t y)
{
    SSD1306.CurrentX = x;
    SSD1306.CurrentY = y;
}

void setContrast(uint8_t contrast, uint8_t precharge, uint8_t comdetect) {
  ssd1306_WriteCommand(SETPRECHARGE); //0xD9
  ssd1306_WriteCommand(precharge); //0xF1 default, to lower the contrast, put 1-1F
  ssd1306_WriteCommand(SETCONTRAST);
  ssd1306_WriteCommand(contrast); // 0-255
  ssd1306_WriteCommand(SETVCOMDETECT); //0xDB, (additionally needed to lower the contrast)
  ssd1306_WriteCommand(comdetect);	//0x40 default, to lower the contrast, put 0
  ssd1306_WriteCommand(DISPLAYALLON_RESUME);
  ssd1306_WriteCommand(DISPLAYON);
}

// https://github.com/ThingPulse/esp8266-oled-ssd1306/blob/5fd04d4bc6c4e0265fcb9c72de0a7b225549454c/src/OLEDDisplay.cpp#L790
void setBrightness(uint8_t brightness)
{
  uint8_t contrast = brightness;
  if (brightness < 128) {
    // Magic values to get a smooth/ step-free transition
    contrast = brightness * 1.171;
  } else {
    contrast = brightness * 1.171 - 43;
  }

  uint8_t precharge = 241;
  if (brightness == 0) {
    precharge = 0;
  }
  uint8_t comdetect = brightness / 8;

  setContrast(contrast, precharge, comdetect);
}

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

static uint16_t ssd1306_GetStringWidth(const char* str, FontDef Font) {
    return strlen(str) * Font.FontWidth;
}

static void ssd1306_WriteCharAt(int16_t x, uint8_t y, char ch, FontDef Font, SSD1306_COLOR color) {
    uint32_t i, b, j;
    // entirely outside? skip
    if (x >= SSD1306_WIDTH || x + (int16_t)Font.FontWidth <= 0) {
        return;
    }
    if (ch < 32 || ch > 126) {
        ch = '?';
    }
    // write the font partially if outside, for scrolling
    for (i = 0; i < Font.FontHeight; i++) {
        b = Font.data[(ch - 32) * Font.FontHeight + i];
        for (j = 0; j < Font.FontWidth; j++) {
            int16_t px = x + j;
            if (px >= 0 && px < SSD1306_WIDTH) {
                if ((b << j) & 0x8000) {
                    ssd1306_DrawPixel(px, y + i, color);
                } else {
                    ssd1306_DrawPixel(px, y + i, (SSD1306_COLOR)!color); // black
                }
            }
        }
    }
}

static void ssd1306_WriteStringAt(int16_t x, uint8_t y, const char* str, FontDef Font, SSD1306_COLOR color) {
    while (*str) {
        if (x >= SSD1306_WIDTH) {
            break;
        }
        if (x + (int16_t)Font.FontWidth > 0) {
            ssd1306_WriteCharAt(x, y, *str, Font, color);
        }
        x += Font.FontWidth;
        str++;
    }
}


void ssd1306_WriteStringMarquee(const char* str, FontDef Font, SSD1306_COLOR color,
                                 uint8_t max_width, MarqueeState_t *state,
                                 uint8_t scroll_speed, uint16_t start_delay, uint8_t loop_gap) {
    if (str == NULL || state == NULL) {
        return;
    }
    
    uint16_t text_width = ssd1306_GetStringWidth(str, Font);
    uint8_t base_x = SSD1306.CurrentX;
    uint8_t base_y = SSD1306.CurrentY;
    
    if (strncmp(state->last_text, str, sizeof(state->last_text) - 1) != 0) {
        state->scroll_offset = 0;
        state->delay_counter = 0;
        strncpy(state->last_text, str, sizeof(state->last_text) - 1);
        state->last_text[sizeof(state->last_text) - 1] = '\0';
    }
    
    if (text_width <= max_width) {
        ssd1306_WriteString(str, Font, color);
        return;
    }
    
    if (state->delay_counter < start_delay) {
        state->delay_counter++;
        ssd1306_WriteStringAt(base_x, base_y, str, Font, color);
        return;
    }
    
    uint16_t total_cycle_width = text_width + loop_gap;
    
    state->scroll_offset += scroll_speed;
    
    if (state->scroll_offset >= total_cycle_width) {
        state->scroll_offset = 0;
        // make delay again
        state->delay_counter = 0;
    }
    
    int16_t primary_x = base_x - (int16_t)state->scroll_offset;
    ssd1306_WriteStringAt(primary_x, base_y, str, Font, color);
    
    int16_t wrapped_x = primary_x + total_cycle_width;
    if (wrapped_x < max_width) {
        ssd1306_WriteStringAt(wrapped_x, base_y, str, Font, color);
    }
}

