#include "jpeg.h"
#include "esp32c5/rom/tjpgd.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include <string.h>

#define TAG "JPEG_FETCH"

#define JPEG_CHUNK_SIZE 8192
#define JPEG_WORK_SIZE 4096

static uint8_t bitmap_buffer[BITMAP_BYTES];

static int offset_x = 0;
static int offset_y = 0;
static uint16_t target_w = SPI_OLED_WIDTH;
static uint16_t target_h = SPI_OLED_HEIGHT;

static const uint8_t dither_matrix[8][8] = {
    { 0, 32,  8, 40,  2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44,  4, 36, 14, 46,  6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47,  7, 39, 13, 45,  5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21}
};

typedef struct {
    esp_http_client_handle_t client;
    uint8_t chunk[JPEG_CHUNK_SIZE];
    size_t chunk_pos;
    size_t chunk_len;
    bool eof;
} jpeg_stream_ctx_t;

static UINT jpeg_input_func(JDEC* jd, BYTE* buff, UINT len) {
    jpeg_stream_ctx_t* ctx = (jpeg_stream_ctx_t*)jd->device;
    UINT copied = 0;

    while (len > 0 && !ctx->eof) {
        if (ctx->chunk_pos >= ctx->chunk_len) {
            int read_len = esp_http_client_read(ctx->client, (char*)ctx->chunk, sizeof(ctx->chunk));
            if (read_len > 0) {
                ctx->chunk_len = read_len;
                ctx->chunk_pos = 0;
            } else {
                ctx->eof = true;
                if (read_len < 0) {
                    ESP_LOGE(TAG, "HTTP read error");
                }
                ctx->chunk_len = 0;
                break;
            }
        }

        size_t avail = ctx->chunk_len - ctx->chunk_pos;
        size_t take = (len < avail) ? len : avail;

        if (buff) {
            memcpy(buff + copied, ctx->chunk + ctx->chunk_pos, take);
        }
        ctx->chunk_pos += take;
        copied += take;
        len -= take;
    }
    return copied;
}

static UINT jpeg_output_func(JDEC* jd, void* bitmap, JRECT* rect) {
    // Cast to uint8_t for byte-wise access
    // Note: 'bitmap' contains RGB888 data (3 bytes per pixel)
    uint8_t* rgb_data = (uint8_t*)bitmap;

    uint16_t src_w = jd->width;
    uint16_t src_h = jd->height;

    // Width of the decoded MCU block in pixels
    int block_w = rect->right - rect->left + 1;

    // Iterate over every pixel in the decoded SOURCE block
    for (int sy = rect->top; sy <= rect->bottom; sy++) {
        for (int sx = rect->left; sx <= rect->right; sx++) {
            
            // 1. Calculate the index within the MCU block buffer
            //    (local_y * width + local_x) * 3 bytes
            int local_x = sx - rect->left;
            int local_y = sy - rect->top;
            int src_idx = (local_y * block_w + local_x) * 3;

            // 2. Extract RGB and convert to Grayscale
            uint8_t r = rgb_data[src_idx];
            uint8_t g = rgb_data[src_idx + 1];
            uint8_t b = rgb_data[src_idx + 2];
            
            // Standard luminosity formula
            uint8_t gray = (r * 77 + g * 150 + b * 29) >> 8;

            // 3. Map this source pixel to the Target Screen area (Scaling)
            //    We calculate the start and end coordinates on the target canvas
            int dst_x_start = (sx * (int)target_w) / (int)src_w;
            int dst_x_end   = ((sx + 1) * (int)target_w) / (int)src_w;
            int dst_y_start = (sy * (int)target_h) / (int)src_h;
            int dst_y_end   = ((sy + 1) * (int)target_h) / (int)src_h;

            // Ensure we cover at least 1 pixel (essential for high-res source images)
            if (dst_x_end <= dst_x_start) dst_x_end = dst_x_start + 1;
            if (dst_y_end <= dst_y_start) dst_y_end = dst_y_start + 1;

            // 4. Draw the rectangle on the screen buffer
            for (int dy = dst_y_start; dy < dst_y_end; dy++) {
                // Apply the centering offset
                int screen_y = dy - offset_y;
                
                // Clip vertical bounds
                if (screen_y < 0 || screen_y >= SPI_OLED_HEIGHT) continue;

                for (int dx = dst_x_start; dx < dst_x_end; dx++) {
                    // Apply the centering offset
                    int screen_x = dx - offset_x;

                    // Clip horizontal bounds
                    if (screen_x < 0 || screen_x >= SPI_OLED_WIDTH) continue;

                    // 5. Dithering Logic
                    uint8_t thresh = dither_matrix[screen_y % 8][screen_x % 8] << 2; // Scale 0-63 to 0-255
                    
                    // SSD1306 uses vertical page layout: each byte is 8 vertical pixels
                    // byte_idx = x + (y / 8) * width
                    // bit position = y % 8
                    int byte_idx = screen_x + (screen_y / 8) * SPI_OLED_WIDTH;
                    uint8_t bit_mask = 1 << (screen_y % 8);

                    if (gray > thresh) {
                        bitmap_buffer[byte_idx] |= bit_mask;
                    } else {
                        bitmap_buffer[byte_idx] &= ~bit_mask;
                    }
                }
            }
        }
    }
    return 1; // Continue decoding
}

uint8_t* fetch_and_dither(const char* url) {
    memset(bitmap_buffer, 0, sizeof(bitmap_buffer));
    offset_x = 0;
    offset_y = 0;

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return NULL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = esp_http_client_fetch_headers(client);
    if (err < 0) {
        ESP_LOGE(TAG, "Fetch headers failed");
        goto cleanup;
    }

    int status = esp_http_client_get_status_code(client);
    if (status != HttpStatus_Ok) {
        ESP_LOGE(TAG, "HTTP status %d", status);
        goto cleanup;
    }
    ESP_LOGI(TAG, "fetch image?");

    jpeg_stream_ctx_t stream_ctx = {
        .client = client,
        .chunk_pos = 0,
        .chunk_len = 0,
        .eof = false
    };

    JDEC dec;
    uint8_t work[JPEG_WORK_SIZE];
    JRESULT res = jd_prepare(&dec, jpeg_input_func, work, sizeof(work), &stream_ctx);
    if (res != JDR_OK) {
        ESP_LOGE(TAG, "JPEG prepare failed: %d", res);
        goto cleanup;
    }

    uint16_t orig_w = dec.width;
    uint16_t orig_h = dec.height;

    float aspect_src = (float)orig_w / orig_h;
    float aspect_dst = (float)SPI_OLED_WIDTH / SPI_OLED_HEIGHT;

    if (aspect_src > aspect_dst) {
        target_h = SPI_OLED_HEIGHT;
        target_w = (uint16_t)(target_h * aspect_src + 0.5f);
    } else {
        target_w = SPI_OLED_WIDTH;
        target_h = (uint16_t)(target_w / aspect_src + 0.5f);
    }

    offset_x = (target_w > SPI_OLED_WIDTH) ? (target_w - SPI_OLED_WIDTH) / 2 : 0;
    offset_y = (target_h > SPI_OLED_HEIGHT) ? (target_h - SPI_OLED_HEIGHT) / 2 : 0;

    res = jd_decomp(&dec, jpeg_output_func, 0);
    if (res != JDR_OK) {
        ESP_LOGE(TAG, "JPEG decode failed: %d", res);
        memset(bitmap_buffer, 0, sizeof(bitmap_buffer));
    }

cleanup:
    esp_http_client_cleanup(client);
    // return (res == JDR_OK) ? bitmap_buffer : NULL;
    return bitmap_buffer;
}