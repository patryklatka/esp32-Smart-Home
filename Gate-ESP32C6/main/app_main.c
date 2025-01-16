/* MQTT over SSL Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_ota_ops.h"
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "config.h"
static const char *TAG = "ESP32-C6-GATE";

#define UART_QUEUE_SIZE 20
#define UART_BUFFER_SIZE 512

#define TX_PIN 10           // Pin TX
#define RX_PIN 11           // Pin RX
#define QUEUE_SIZE 20       // Rozmiar kolejki zdarzeń

typedef struct {
    char topic[UART_BUFFER_SIZE];
    char data[UART_BUFFER_SIZE];
} mqtt_data_t;

// Kolejki
QueueHandle_t uart_to_mqtt_queue;
QueueHandle_t mqtt_to_uart_queue;

#if CONFIG_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t mqtt_eclipseprojects_io_pem_start[]  = "-----BEGIN CERTIFICATE-----\n" CONFIG_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t mqtt_eclipseprojects_io_pem_start[]   asm("_binary_mqtt_eclipseprojects_io_pem_start");
#endif
extern const uint8_t mqtt_eclipseprojects_io_pem_end[]   asm("_binary_mqtt_eclipseprojects_io_pem_end");

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI("MQTT", "MQTT_EVENT_CONNECTED");
        
        esp_mqtt_client_subscribe(client, "gr1_ui/swiatlo", 0);   
        esp_mqtt_client_subscribe(client, "gr1_ui/wiatrak", 0);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI("MQTT", "Received topic: %.*s", event->topic_len, event->topic);
        ESP_LOGI("MQTT", "Received data: %.*s", event->data_len, event->data);

        // Tworzenie obiektu do wysłania do kolejki
        char formatted_message[UART_BUFFER_SIZE]; 
        int len = snprintf(formatted_message, sizeof(formatted_message), "%.*s: %.*s", 
                    event->topic_len, event->topic, event->data_len, event->data);
        // Wysyłamy dane i temat do kolejki
        if (xQueueSend(mqtt_to_uart_queue, formatted_message, portMAX_DELAY) != pdPASS) {
            ESP_LOGW("Queue", "Failed to send data to mqtt_to_uart_queue");
        }
        break;

    default:
        break;
    }
}


static void mqtt_to_uart_task(void *param)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)param;
    char formatted_message[UART_BUFFER_SIZE]; // Bufor na sformatowaną wiadomość

    while (1) {
        // Czekamy na dane z MQTT (np. z funkcji store_mqtt_data)
        if (xQueueReceive(mqtt_to_uart_queue, formatted_message, portMAX_DELAY) == pdPASS) {
            // Przesyłamy dane przez UART
            uart_write_bytes(UART_NUM_1, formatted_message, strlen(formatted_message));
            ESP_LOGI(TAG, "Message sent via UART: %s", formatted_message);
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Czekamy chwilę przed kolejną iteracją
    }
}


static void uart_to_mqtt_task(void *param)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)param;

    uint8_t data[UART_BUFFER_SIZE];
    while (1) {
        int len = uart_read_bytes(UART_NUM_1, data, UART_BUFFER_SIZE, pdMS_TO_TICKS(100)); // Czekamy na dane przez 100ms
        if (len > 0) {
            data[len] = '\0'; // Zakończymy łańcuch znaków

            ESP_LOGI(TAG, "Message read via UART: %s", data);
            // Wysyłamy dane do kolejki
            if (xQueueSend(uart_to_mqtt_queue, data, portMAX_DELAY) != pdPASS) {
                ESP_LOGW("Queue", "Failed to send data to uart_to_mqtt_queue");
            }
        }

    }
}

static void mqtt_publish_task(void *param)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)param;

    char data[UART_BUFFER_SIZE];
    while (1) {
        // Czekamy na dane w kolejce
        if (xQueueReceive(uart_to_mqtt_queue, data, portMAX_DELAY) == pdPASS) {
            // Publikujemy dane na brokerze MQTT
            float temperature = 0.0f, humidity = 0.0f;
            if (sscanf(data, "{temperature: %f, humidity: %f}", &temperature, &humidity) == 2) {
                // Publikujemy dane temperatury i wilgotności na odpowiednich tematach
                char temp_msg[10], humidity_msg[10];
                snprintf(temp_msg, sizeof(temp_msg), "%.2f", temperature);
                snprintf(humidity_msg, sizeof(humidity_msg), "%.2f", humidity);

                int temp_msg_id = esp_mqtt_client_publish(client, "gr1/temperature", temp_msg, 0, 0, 0);
                int humidity_msg_id = esp_mqtt_client_publish(client, "gr1/wilgotnosc", humidity_msg, 0, 0, 0);

                ESP_LOGI(TAG, "Published gr1/temperature: %.2f", temperature);
                ESP_LOGI(TAG, "Published gr1/wilgotnosc: %.2f", humidity);
                if (strstr(data, "fan_state: 1")) {
                    // Publikujemy wiadomość o stanie światła włączonym
                    int msg_id = esp_mqtt_client_publish(client, "gr1/wiatrak", "on", 0, 0, 0);
                    ESP_LOGI(TAG, "Published gr1/wiatrak: on   with msg_id=%d", msg_id);
                } else if (strstr(data, "fan_state: 0")) {
                    // Publikujemy wiadomość o stanie światła wyłączonym
                    int msg_id = esp_mqtt_client_publish(client, "gr1/wiatrak", "off", 0, 0, 0);
                    ESP_LOGI(TAG, "Published gr1/wiatrak: off   with msg_id=%d", msg_id);
                }
                } 

            else if (strstr(data, "light_state: 1")) {
                // Publikujemy wiadomość o stanie światła włączonym
                int msg_id = esp_mqtt_client_publish(client, "gr1/swiatlo", "on", 0, 0, 0);
                ESP_LOGI(TAG, "Published gr1/swiatlo: on   with msg_id=%d", msg_id);
            } else if (strstr(data, "light_state: 0")) {
                // Publikujemy wiadomość o stanie światła wyłączonym
                int msg_id = esp_mqtt_client_publish(client, "gr1/swiatlo", "off", 0, 0, 0);
                ESP_LOGI(TAG, "Published gr1/swiatlo: off   with msg_id=%d", msg_id);
            } else if (strstr(data, "fan_state: 1")) {
                // Publikujemy wiadomość o stanie wentylatora włączonym
                int msg_id = esp_mqtt_client_publish(client, "gr1/wiatrak", "on", 0, 0, 0);
                ESP_LOGI(TAG, "Published gr1/wiatrak: on   with msg_id=%d", msg_id);
            } else if (strstr(data, "fan_state: 0")) {
                // Publikujemy wiadomość o stanie wentylatora wyłączonym
                int msg_id = esp_mqtt_client_publish(client, "gr1/wiatrak", "off", 0, 0, 0);
                ESP_LOGI(TAG, "Published gr1/wiatrak: off   with msg_id=%d", msg_id);
            } 
    
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Czekamy chwilę przed kolejną iteracją
}


static void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = "mqtts://1855d1e75c264a00b0fdffc55e0ec025.s1.eu.hivemq.cloud:8883",
            .verification.certificate = (const char *)mqtt_eclipseprojects_io_pem_start
        },
        .credentials = {
            .username = USERNAME,
            .authentication = {
                .password = PASSWORD
            }
        },
    };

    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    // Tworzymy zadania z przekazaniem wskaźnika na klienta
    xTaskCreate(mqtt_to_uart_task, "mqtt_to_uart", 4096, client, 5, NULL);
    xTaskCreate(uart_to_mqtt_task, "uart_to_mqtt", 4096, client, 5, NULL);
    xTaskCreate(mqtt_publish_task, "mqtt_publish_task", 4096, client, 5, NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    // Konfiguracja UART
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_NUM_1, &uart_config);
    uart_driver_install(UART_NUM_1, UART_BUFFER_SIZE, UART_BUFFER_SIZE, QUEUE_SIZE, NULL, 0);
    uart_set_pin(UART_NUM_1, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Tworzenie kolejki UART
    uart_to_mqtt_queue = xQueueCreate(UART_QUEUE_SIZE, UART_BUFFER_SIZE);
    if (uart_to_mqtt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UART queue");
    }
    mqtt_to_uart_queue = xQueueCreate(UART_QUEUE_SIZE, UART_BUFFER_SIZE);
    if (mqtt_to_uart_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UART queue");
    }

    mqtt_app_start();
}
