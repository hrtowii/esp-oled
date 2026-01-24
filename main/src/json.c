#include "esp_http_client.h" 
#include "esp_tls.h" 
#include "cJSON.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"

static const char *TAG = "HTTP_CLIENT";

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
	static int output_len; // Stores number of bytes read
	switch(evt->event_id) {
		case HTTP_EVENT_ERROR:
			ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
			break;
		case HTTP_EVENT_ON_CONNECTED:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
			break;
		case HTTP_EVENT_HEADER_SENT:
			ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
			break;
		case HTTP_EVENT_ON_HEADER:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
			break;
		case HTTP_EVENT_ON_DATA:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
			//ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, content_length=%d", esp_http_client_get_content_length(evt->client));
			ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, output_len=%d", output_len);
			// If user_data buffer is configured, copy the response into the buffer
			if (evt->user_data) {
				memcpy(evt->user_data + output_len, evt->data, evt->data_len);
			}
			output_len += evt->data_len;
			break;
		case HTTP_EVENT_ON_FINISH:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
			output_len = 0;
			break;
        case HTTP_EVENT_ON_STATUS_CODE:
        output_len = 0;
            break;
        case HTTP_EVENT_ON_HEADERS_COMPLETE:
        output_len = 0;
            break;
		case HTTP_EVENT_DISCONNECTED:
			ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
			int mbedtls_err = 0;
			esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
			if (err != 0) {
				output_len = 0;
				ESP_LOGE(TAG, "Last esp error code: 0x%x", err);
				ESP_LOGE(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
			}
			break;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
		case HTTP_EVENT_REDIRECT:
			ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
			break;
#endif
	}
	return ESP_OK;
}
int http_client_content_length(const char *url)
{
    ESP_LOGI(TAG, "Checking length for: %s", url);
    int content_length = 0;
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach, // <--- MAGIC LINE: Auto-handles SSL certs
        .timeout_ms = 5000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // We only need the headers to get the length
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        content_length = esp_http_client_get_content_length(client);
        ESP_LOGD(TAG, "Status = %d, Content_Length = %d", 
                 esp_http_client_get_status_code(client), content_length);
    } else {
        ESP_LOGE(TAG, "Length check failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    return content_length;
}

esp_err_t http_client_content_get(const char *url, char *response_buffer)
{
    ESP_LOGI(TAG, "Downloading: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = response_buffer,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .buffer_size = 2048,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Download Status = %d, len = %lld", 
                 esp_http_client_get_status_code(client), 
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Download failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    return err;
}

// Returns pointer to malloc'd buffer, or NULL on failure.
// Caller is responsible for free().
char* http_client_get_image(const char *url)
{
    int content_length = 0;
    
    for (int retry = 0; retry < 3; retry++) {
        content_length = http_client_content_length(url);
        if (content_length > 0) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (content_length <= 0) {
        ESP_LOGE(TAG, "Failed to get content length or empty file");
        return NULL;
    }

    // Allocate memory (+1 for safety null terminator, though not strictly needed for binary images)
    char *response_buffer = (char *) malloc(content_length + 1);
    if (response_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes", content_length);
        return NULL;
    }
    
    // memset(response_buffer, 0, content_length + 1); 

    esp_err_t err = http_client_content_get(url, response_buffer);
    
    if (err != ESP_OK) {
        free(response_buffer);
        return NULL;
    }

    return response_buffer;
}