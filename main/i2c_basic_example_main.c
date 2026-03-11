/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Hall Sensor RPM Meter Example

   RPM meter using hall sensor digital output only.
   Detects rotation by monitoring edge transitions on digital output.
   Prints updates only when RPM value changes.
*/
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_intr_alloc.h"

static const char *TAG = "rpm_meter";

/* RPM Meter Configuration */
#define RPM_DISPLAY_INTERVAL_MS      500        /*!< Display RPM every 500ms */
#define RPM_MIN_PERIOD_MS            25         /*!< Minimum time between detections (max RPM ~2000) */
#define RPM_MAX_PERIOD_MS            10000      /*!< Maximum time between detections (min RPM ~6) */
#define RPM_AVERAGE_COUNT            5          /*!< Number of recent measurements to average */
#define RPM_QUEUE_SIZE               10         /*!< Size of interrupt event queue */

/* Hall Sensor Pin Configuration */
#define HALL_DIGITAL_GPIO            4          /*!< GPIO pin for digital output (change as needed) */

/* RPM Meter State */
typedef struct {
    int64_t last_detection_time_us;       /*!< Timestamp of last detection */
    int64_t period_us[RPM_AVERAGE_COUNT]; /*!< Array of recent period measurements */
    uint8_t period_index;                 /*!< Current index in period array */
    uint8_t period_count;                 /*!< Number of valid periods */
    uint32_t total_detections;            /*!< Total number of detections since start */
    float last_rpm;                       /*!< Last displayed RPM value */
} rpm_meter_state_t;

/* Global state and queue */
static rpm_meter_state_t g_rpm_state;
static QueueHandle_t g_gpio_evt_queue = NULL;

/**
 * @brief GPIO interrupt handler - called from ISR
 */
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    /* Send GPIO number to queue from ISR */
    xQueueSendFromISR(g_gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Initialize GPIO with interrupt for digital hall sensor reading
 */
static esp_err_t gpio_init(void)
{
    /* Create queue for GPIO events */
    g_gpio_evt_queue = xQueueCreate(RPM_QUEUE_SIZE, sizeof(uint32_t));
    if (g_gpio_evt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create GPIO event queue");
        return ESP_FAIL;
    }

    /* Configure GPIO */
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,  /* Interrupt on rising edge only */
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << HALL_DIGITAL_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO");
        return ret;
    }

    /* Install GPIO ISR service */
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service");
        return ret;
    }

    /* Hook ISR handler for specific GPIO pin */
    ret = gpio_isr_handler_add(HALL_DIGITAL_GPIO, gpio_isr_handler, (void*) HALL_DIGITAL_GPIO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GPIO ISR handler");
        return ret;
    }

    ESP_LOGI(TAG, "GPIO interrupt initialized successfully (pin %d)", HALL_DIGITAL_GPIO);
    return ESP_OK;
}

/**
 * @brief Initialize RPM meter state
 */
static void rpm_meter_init(rpm_meter_state_t *state)
{
    memset(state, 0, sizeof(rpm_meter_state_t));
    state->last_detection_time_us = 0;
    state->last_rpm = -1.0f; /* Initialize to invalid value to force first print */
}

/**
 * @brief Process rising edge detection from interrupt
 * Returns true if a new valid detection occurred
 */
static bool rpm_meter_process_edge(rpm_meter_state_t *state)
{
    int64_t current_time_us = esp_timer_get_time();
    bool detection_occurred = false;

    /* Count all detections, including the first one */
    state->total_detections++;
    
    if (state->last_detection_time_us > 0) {
        int64_t period_us = current_time_us - state->last_detection_time_us;
        int64_t period_ms = period_us / 1000;
        
        /* Validate period is within reasonable range */
        if (period_ms >= RPM_MIN_PERIOD_MS && period_ms <= RPM_MAX_PERIOD_MS) {
            /* Store period in array for averaging */
            state->period_us[state->period_index] = period_us;
            state->period_index = (state->period_index + 1) % RPM_AVERAGE_COUNT;
            if (state->period_count < RPM_AVERAGE_COUNT) {
                state->period_count++;
            }
            detection_occurred = true;
        }
    } else {
        /* First detection - just record the time, no period yet */
        detection_occurred = true;
    }
    /* Always update detection time on valid edge */
    state->last_detection_time_us = current_time_us;
    
    return detection_occurred;
}

/**
 * @brief Calculate average period from stored measurements (in milliseconds)
 */
static float rpm_meter_get_avg_period_ms(rpm_meter_state_t *state)
{
    if (state->period_count == 0) {
        return 0.0f;
    }

    /* Calculate average period - always use the most recent measurements */
    uint8_t count_to_use = (state->period_count < RPM_AVERAGE_COUNT) ? 
                            state->period_count : RPM_AVERAGE_COUNT;
    
    int64_t sum_period_us = 0;
    
    /* Read from the most recent position backwards to get the latest measurements */
    if (state->period_count >= RPM_AVERAGE_COUNT) {
        /* Buffer is full, read from most recent backwards */
        for (int i = 0; i < count_to_use; i++) {
            int idx = (state->period_index - 1 - i + RPM_AVERAGE_COUNT) % RPM_AVERAGE_COUNT;
            sum_period_us += state->period_us[idx];
        }
    } else {
        /* Buffer not full yet, read sequentially */
        for (int i = 0; i < count_to_use; i++) {
            sum_period_us += state->period_us[i];
        }
    }
    
    int64_t avg_period_us = sum_period_us / count_to_use;
    float period_ms = (float)avg_period_us / 1000.0f;

    return period_ms;
}

/**
 * @brief Calculate RPM from stored period measurements
 */
static float rpm_meter_calculate_rpm(rpm_meter_state_t *state)
{
    float period_ms = rpm_meter_get_avg_period_ms(state);
    if (period_ms <= 0.0f) {
        return 0.0f;
    }

    /* Convert period to RPM: RPM = 60 / (period_in_seconds) */
    float period_seconds = period_ms / 1000.0f;
    float rpm = 60.0f / period_seconds;

    return rpm;
}


void app_main(void)
{
    uint32_t io_num;
    int64_t last_display_time = 0;

    /* Initialize GPIO with interrupt */
    ESP_ERROR_CHECK(gpio_init());

    ESP_LOGI(TAG, "Hall sensor initialized successfully");
    ESP_LOGI(TAG, "Digital pin: %d (interrupt mode)", HALL_DIGITAL_GPIO);

    /* Initialize RPM meter state */
    rpm_meter_init(&g_rpm_state);

    ESP_LOGI(TAG, "RPM meter ready. Monitoring rotation...");
    ESP_LOGI(TAG, "Place a magnet on a rotating object near the hall sensor.");

    /* Main RPM measurement loop - processes interrupts from queue */
    while (1) {
        /* Wait for GPIO interrupt event from queue */
        if (xQueueReceive(g_gpio_evt_queue, &io_num, portMAX_DELAY)) {
            /* Process the rising edge detection */
            bool detection_occurred = rpm_meter_process_edge(&g_rpm_state);
            
            /* Calculate current RPM */
            float current_rpm = rpm_meter_calculate_rpm(&g_rpm_state);
            
            /* Print on detection or periodically */
            int64_t current_time = esp_timer_get_time() / 1000; /* Convert to ms */
            bool should_print = false;
            
            if (detection_occurred) {
                should_print = true;
            } else if (current_rpm > 0.0f && current_rpm != g_rpm_state.last_rpm) {
                should_print = true;
            } else if (current_time - last_display_time >= RPM_DISPLAY_INTERVAL_MS) {
                should_print = true;
            }
            
            if (should_print) {
                if (current_rpm > 0.0f) {
                    float period_ms = rpm_meter_get_avg_period_ms(&g_rpm_state);
                    ESP_LOGI(TAG, "RPM: %.1f | Period: %.1f ms | Samples: %d/%d | Total: %lu", 
                             current_rpm, period_ms, g_rpm_state.period_count, RPM_AVERAGE_COUNT, 
                             (unsigned long)g_rpm_state.total_detections);
                } else {
                    ESP_LOGI(TAG, "RPM: -- | Waiting for rotation...");
                }
                g_rpm_state.last_rpm = current_rpm;
                last_display_time = current_time;
            }
        }
    }
}
