#ifndef FETCHER_H
#define FETCHER_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    char artist[64];
    char song[64];
    bool is_playing;
    int64_t start_time_ms;
    int64_t end_time_ms;
} app_state_t;

extern app_state_t g_app_state;
extern SemaphoreHandle_t xStateMutex;

int64_t get_current_time_ms(void);
void start_fetcher_task(void);

#endif // FETCHER_H
