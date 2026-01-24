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

#define TAG "FETCHER"
#define LANYARD_ID     "413331641109446656" 
#define LANYARD_URI    "wss://api.lanyard.rest/socket"
char album_art_url[128];

esp_websocket_client_handle_t client = NULL;
TaskHandle_t heartbeat_task_handle = NULL;
app_state_t g_app_state;
QueueHandle_t g_image_queue;
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
        ESP_LOGD(TAG, "heartbeatping");
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
            ESP_LOGI(TAG, "connected!");
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == WS_TRANSPORT_OPCODES_TEXT && data->data_len > 0) {
                char *json_str = strndup((const char *)data->data_ptr, data->data_len);
                cJSON *root = cJSON_Parse(json_str);
                free(json_str);

                if (!root) return;
                
                cJSON *op_item = cJSON_GetObjectItem(root, "op");
                if (!cJSON_IsNumber(op_item)) return;
                int op = (int)cJSON_GetNumberValue(op_item);

                if (op == 1) {
                    cJSON *data_obj = cJSON_GetObjectItem(root, "d");
                    cJSON *interval_item = cJSON_GetObjectItem(data_obj, "heartbeat_interval");
                    if (!cJSON_IsNumber(interval_item)) return;
                    int interval = (int)cJSON_GetNumberValue(interval_item);
                    
                    if (heartbeat_task_handle == NULL) {
                        xTaskCreate(heartbeat_task, "ws_hb", 2048, (void *)interval, 5, &heartbeat_task_handle);
                    }

                    // ESP_LOGI(TAG, "subscribe id: %s", LANYARD_ID);
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
                            cJSON *artist_item = cJSON_GetObjectItem(spotify, "artist");
                            cJSON *song_item = cJSON_GetObjectItem(spotify, "song");
                            cJSON *album_item = cJSON_GetObjectItem(spotify, "album_art_url");
                            char album_art_url[128];
strncpy(album_art_url, cJSON_GetStringValue(album_item) ?: "", sizeof(album_art_url) - 1);
album_art_url[127] = '\0';
                            if (!cJSON_IsString(artist_item) || !cJSON_IsString(song_item) || !cJSON_IsString(album_item)) {
                                ESP_LOGE(TAG, "either artist or song or art url doesn't exist, skip!");
                                xSemaphoreGive(xStateMutex);
                                return;
                            }
                            char *art = cJSON_GetStringValue(artist_item);
                            char *sng = cJSON_GetStringValue(song_item);
                            char *album_art = cJSON_GetStringValue(album_item);
                            cJSON *ts = cJSON_GetObjectItem(spotify, "timestamps");
                            if (ts) {
                                cJSON *start_item = cJSON_GetObjectItem(ts, "start");
                                cJSON *end_item = cJSON_GetObjectItem(ts, "end");
                                if (cJSON_IsNumber(start_item) && cJSON_IsNumber(end_item)) {
                                    g_app_state.start_time_ms = (int64_t)cJSON_GetNumberValue(start_item);
                                    g_app_state.end_time_ms   = (int64_t)cJSON_GetNumberValue(end_item);
                                } else {
                                    g_app_state.start_time_ms = 0;
                                }
                            } else {
                                g_app_state.start_time_ms = 0;
                            }

                            strncpy(g_app_state.artist, art, 63);
                            strncpy(g_app_state.song, sng, 63);
                            // strncpy(g_app_state.album_art, album_art, 63); im not even using this struct in g app state lol its all in a queue
                            xQueueSend(
                                g_image_queue,
                                album_art_url,
                                portMAX_DELAY
                            );
                            g_app_state.is_playing = true;
                            ESP_LOGI(TAG, "playing %s", sng);
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

const char cert[] = 
"-----BEGIN CERTIFICATE-----\n"
"MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD\n"
"VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\n"
"A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\n"
"WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\n"
"IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\n"
"AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi\n"
"QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR\n"
"HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\n"
"BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D\n"
"9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8\n"
"p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD\n"
"-----END CERTIFICATE-----";


void websocket_app_start() {
    if (xStateMutex == NULL) {
        xStateMutex = xSemaphoreCreateMutex();
    }
    if (g_image_queue == NULL) {
        g_image_queue = xQueueCreate(
            1, // ?? i dunno bruh i can just pop out of it as soon as i get it right
            64
        );
    }
    esp_websocket_client_config_t config = {
        .uri = LANYARD_URI,
        .buffer_size = 4096,
        .disable_auto_reconnect = false,
        .cert_pem = cert,
        // .crt_bundle_attach = esp_crt_bundle_attach,
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
