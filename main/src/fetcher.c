// web fetcher
// https://github.com/espressif/esp-idf/blob/v5.5.2/examples/protocols/esp_http_client/main/esp_http_client_example.c
// api for bus: https://github.com/cheeaun/arrivelah
// bus ids maybe hardcoded for now? just do like 972
// 45009
// https://arrivelah2.busrouter.sg/

// other option: websocket for music rn via lanyard
#include "esp_websocket_client.h"
#include <sys/time.h>
#include "json.h"
#include "cJSON.h"
#include "esp_log.h"
#include "fetcher.h"
#include "esp_crt_bundle.h"

#define TAG "FETCHER: "
#define LANYARD_ID     "413331641109446656" 
#define LANYARD_URI    "wss://api.lanyard.rest/socket"

esp_websocket_client_handle_t client = NULL;
TaskHandle_t heartbeat_task_handle = NULL;
app_state_t g_app_state;
SemaphoreHandle_t xStateMutex = NULL;

int64_t get_current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000LL) + (tv.tv_usec / 1000LL);
}

void ws_send_json(cJSON *json) {
    char *txt = cJSON_PrintUnformatted(json);
    if (esp_websocket_client_is_connected(client)) {
        esp_websocket_client_send_text(client, txt, strlen(txt), pdMS_TO_TICKS(500));
    }
    free(txt);
    cJSON_Delete(json);
}


void heartbeat_task(void *pvParameters) {
    int interval_ms = (int)pvParameters;
    TickType_t xDelay = pdMS_TO_TICKS(interval_ms);
    
    while (1) {
        ESP_LOGD(TAG, "Ping...");
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "op", 3);
        ws_send_json(root);
        vTaskDelay(xDelay);
    }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WS Connected");
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == WS_TRANSPORT_OPCODES_TEXT && data->data_len > 0) {
                char *json_str = strndup((const char *)data->data_ptr, data->data_len);
                cJSON *root = cJSON_Parse(json_str);
                free(json_str);

                if (!root) return;

                int op = cJSON_GetObjectItem(root, "op")->valueint;

                if (op == 1) {
                    int interval = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "d"), "heartbeat_interval")->valueint;
                    
                    if (heartbeat_task_handle == NULL) {
                        xTaskCreate(heartbeat_task, "ws_hb", 2048, (void *)interval, 5, &heartbeat_task_handle);
                    }

                    ESP_LOGI(TAG, "subscribe id: %s", LANYARD_ID);
                    cJSON *sub = cJSON_CreateObject();
                    cJSON_AddNumberToObject(sub, "op", 2);
                    cJSON *d = cJSON_CreateObject();
                    cJSON *ids = cJSON_CreateArray();
                    cJSON_AddItemToArray(ids, cJSON_CreateString(LANYARD_ID));
                    cJSON_AddItemToObject(d, "subscribe_to_ids", ids);
                    cJSON_AddItemToObject(sub, "d", d);
                    ws_send_json(sub);
                } 
                
                else if (op == 0) {
                    cJSON *d = cJSON_GetObjectItem(root, "d");
                    cJSON *spotify = cJSON_GetObjectItem(d, "spotify");

                    if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(500))) {
                        if (cJSON_IsObject(spotify)) {
                            // Extract Strings
                            char *art = cJSON_GetObjectItem(spotify, "artist")->valuestring;
                            char *sng = cJSON_GetObjectItem(spotify, "song")->valuestring;
                            
                            cJSON *ts = cJSON_GetObjectItem(spotify, "timestamps");
                            if (ts) {
                                g_app_state.start_time_ms = (int64_t)cJSON_GetObjectItem(ts, "start")->valuedouble;
                                g_app_state.end_time_ms   = (int64_t)cJSON_GetObjectItem(ts, "end")->valuedouble;
                            } else {
                                g_app_state.start_time_ms = 0;
                            }

                            strncpy(g_app_state.artist, art, 63);
                            strncpy(g_app_state.song, sng, 63);
                            g_app_state.is_playing = true;
                            ESP_LOGI(TAG, "playing: %s", sng);
                        } else {
                            g_app_state.is_playing = false;
                        }
                        xSemaphoreGive(xStateMutex);
                    }
                }
                cJSON_Delete(root);
            }
            break;
            
        case WEBSOCKET_EVENT_DISCONNECTED:
             ESP_LOGW(TAG, "WS Disconnected");
             if (heartbeat_task_handle) {
                 vTaskDelete(heartbeat_task_handle);
                 heartbeat_task_handle = NULL;
             }
             break;
    }
}


void websocket_app_start() {
    if (xStateMutex == NULL) {
        xStateMutex = xSemaphoreCreateMutex();
    }
    esp_websocket_client_config_t config = {
        .uri = LANYARD_URI,
        .buffer_size = 4096,
        .disable_auto_reconnect = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    client = esp_websocket_client_init(&config);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    esp_websocket_client_start(client);
}

void fetcher_task_entry(void *arg) {
    websocket_app_start();
    vTaskDelete(NULL);
}

void start_fetcher_task(void) {
    xTaskCreate(fetcher_task_entry, "fetcher_init", 4096, NULL, 5, NULL);
}
