#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_openthread.h"
#include "openthread/udp.h"
#include "openthread/message.h"
#include "esp_openthread_netif_glue.h"
#include "esp_ot_config.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "openthread/thread.h"
#include "openthread/link.h"
#include "openthread/platform/radio.h"
#include "esp_vfs_eventfd.h"
#include "openthread/logging.h"
#include "esp_event.h"
#include "esp_task.h"
#include "esp_tls.h"
#include "freertos/queue.h"
#include "driver/uart.h"

#define TAG "ESP32-H2-GATE"
#define THREAD_UDP_PORT 12345 // Port, na którym nasłuchujemy danych
#define UART_QUEUE_SIZE 10
#define UART_BUFFER_SIZE 512


#define TX_PIN 10           // Pin TX
#define RX_PIN 11           // Pin RX
#define QUEUE_SIZE 20       // Rozmiar kolejki zdarzeń



static QueueHandle_t uart_write_queue;
static QueueHandle_t uart_read_queue;
static otUdpSocket sUdpSocket;

typedef struct {
    float temperature;
    float humidity;
    bool fan_state;
    bool light_state;

} sensor_data;

typedef struct {
    char topic[UART_BUFFER_SIZE];
    char data[UART_BUFFER_SIZE];
} mqtt_data_t;

static sensor_data current_sensor_data = {0};

// Funkcja do wysyłania danych UDP
void udp_send_data(const char *message) {
    otError error;
    otMessageInfo messageInfo;
    otIp6Address  destinationAddr;
    otInstance *sInstance = esp_openthread_get_instance();
    if (!sInstance) {
        ESP_LOGE(TAG, "OpenThread instance not initialized.");
        return;
    }

    // Sprawdzanie, czy urządzenie jest podłączone do sieci Thread
    otDeviceRole role = otThreadGetDeviceRole(sInstance);
    if (role == OT_DEVICE_ROLE_DISABLED || role == OT_DEVICE_ROLE_DETACHED) {
        ESP_LOGW(TAG, "Device is not in a valid state for sending messages (Role: %d).", role);
        return;
    }

    memset(&messageInfo, 0, sizeof(messageInfo));
    otIp6AddressFromString("ff03::1", &destinationAddr);
    messageInfo.mPeerAddr = destinationAddr;
    messageInfo.mPeerPort = THREAD_UDP_PORT;

    otMessage *msg = otUdpNewMessage(sInstance, NULL);
    if (msg == NULL) {
        ESP_LOGE(TAG, "Failed to create message");
        return;
    }

    // Tworzenie wiadomości
    error = otMessageAppend(msg, message, strlen(message));
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to append message, error: %d", error);
        otMessageFree(msg);
        return;
    }

    // Wysyłanie wiadomości
    error = otUdpSend(sInstance, &sUdpSocket, msg, &messageInfo);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to send message, error: %d", error);
    } else {
        // ESP_LOGI(TAG, "Message sent via Thread: %s", message);
    }
}

// Callback do odbioru danych
static void udp_receive_callback(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    char buffer[128]; // Bufor na odebrane dane
    int length = otMessageRead(aMessage, otMessageGetOffset(aMessage), buffer, sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0'; // Dodaj zakończenie ciągu znaków
        ESP_LOGI(TAG, "Received message from Thread: %s", buffer);

        char uart_message[UART_BUFFER_SIZE];

        // Szukanie "temperature" i "humidity"
        if (strstr(buffer, "temperature") && strstr(buffer, "humidity")) {
            float temperature = 0, humidity = 0;

            if (sscanf(buffer, "{temperature: %f, humidity: %f}", &temperature, &humidity) == 2) {
                snprintf(uart_message, sizeof(uart_message), "{temperature: %.2f, humidity: %.2f}", temperature, humidity);
                xQueueSend(uart_write_queue, uart_message, portMAX_DELAY);
            }

        // Szukanie "fan_state"
        } else if (strstr(buffer, "fan_state")) {
            int fan_state = 0;

            if (sscanf(buffer, "{fan_state: %d}", &fan_state) == 1) {
                snprintf(uart_message, sizeof(uart_message), "{fan_state: %d}", fan_state);
                xQueueSend(uart_write_queue, uart_message, portMAX_DELAY);
            }

        // Szukanie "light_state"
        } else if (strstr(buffer, "light_state")) {
            int light_state = 0;

            if (sscanf(buffer, "{light_state: %d}", &light_state) == 1) {
                snprintf(uart_message, sizeof(uart_message), "{light_state: %d}", light_state);
                xQueueSend(uart_write_queue, uart_message, portMAX_DELAY);
            }
        }
    } else {
        ESP_LOGW(TAG, "Failed to read UDP message.");
    }
}

// Funkcja inicjalizująca gniazdo UDP
static void init_udp_receiver(otInstance *instance)
{
    memset(&sUdpSocket, 0, sizeof(sUdpSocket));

    otSockAddr listenSockAddr;
    memset(&listenSockAddr, 0, sizeof(listenSockAddr));
    listenSockAddr.mPort = THREAD_UDP_PORT; // Ustaw port dla nasłuchiwania

    ESP_ERROR_CHECK(otUdpOpen(instance, &sUdpSocket, udp_receive_callback, NULL));
    ESP_ERROR_CHECK(otUdpBind(instance, &sUdpSocket, &listenSockAddr, OT_NETIF_THREAD));
    ESP_LOGI(TAG, "UDP socket initialized and bound to port %d", THREAD_UDP_PORT);
}

// Task do obsługi odbioru danych
static void udp_to_uart_task(void *arg)
{
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGE(TAG, "OpenThread instance is not initialized.");
        vTaskDelete(NULL);
    }

    init_udp_receiver(instance);

    ESP_LOGI(TAG, "UDP receiver task started.");
    while (1) {
        // Obsługa pętli UDP jest automatyczna w OpenThread, więc nic więcej nie musimy tutaj robić
        vTaskDelay(pdMS_TO_TICKS(1000)); // Po prostu czekamy
    }
}


/////////////////////////////////////////////////
// Konfiguracja sieci

// Funkcja konfigurująca sieć Thread jako End Device
static void configure_thread_network(otInstance *instance) {
    ESP_ERROR_CHECK(esp_openthread_auto_start(NULL)); // Automatyczny start Thread
    ESP_LOGI(TAG, "Thread network configured as End Device.");
}

// Funkcja inicjalizująca OpenThread i sieć
static esp_netif_t *init_openthread_netif(const esp_openthread_platform_config_t *config) {
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *netif = esp_netif_new(&cfg);
    assert(netif != NULL);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_openthread_netif_glue_init(config)));

    return netif;
}

// Task do konfiguracji i uruchamiania OpenThread
static void ot_task_worker(void *pvParameter)
{
    // Konfiguracja platformy OpenThread
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };

    // Inicjalizacja stosu OpenThread
    ESP_ERROR_CHECK(esp_openthread_init(&config));

#if CONFIG_OPENTHREAD_LOG_LEVEL_DYNAMIC
    (void)otLoggingSetLevel(CONFIG_LOG_DEFAULT_LEVEL);
#endif

    esp_netif_t *openthread_netif;
    openthread_netif = init_openthread_netif(&config);
    esp_netif_set_default_netif(openthread_netif);

    // Konfiguracja sieci jako urządzenie końcowe
    configure_thread_network(esp_openthread_get_instance());

    // Uruchom główną pętlę OpenThread
    esp_openthread_launch_mainloop();

    // Sprzątanie (nie będzie wywoływane w normalnym scenariuszu)
    esp_openthread_netif_glue_deinit();
    esp_netif_destroy(openthread_netif);
    esp_vfs_eventfd_unregister();
    vTaskDelete(NULL);
}

// Task do wypisywania statusu urządzenia co 5 sekund
static void print_device_status_task(void *arg)
{
    while (1)
    {
        otInstance *instance = esp_openthread_get_instance();
        if (instance)
        {
            otDeviceRole role = otThreadGetDeviceRole(instance);
            const char *role_str = "Unknown";
            switch (role)
            {
            case OT_DEVICE_ROLE_DISABLED:
                role_str = "Disabled";
                break;
            case OT_DEVICE_ROLE_DETACHED:
                role_str = "Detached";
                break;
            case OT_DEVICE_ROLE_CHILD:
                role_str = "End Device";
                break;
            case OT_DEVICE_ROLE_ROUTER:
                role_str = "Router";
                break;
            case OT_DEVICE_ROLE_LEADER:
                role_str = "Leader";
                break;
            }

            ESP_LOGI(TAG, "Device role: %s", role_str);
        }
        else
        {
            ESP_LOGW(TAG, "OpenThread instance not initialized.");
        }
        vTaskDelay(pdMS_TO_TICKS(120000)); 
    }
}


void uart_write_task(void *pvParameters) {
    char uart_buffer[UART_BUFFER_SIZE];

    while (1) {
        if (xQueueReceive(uart_write_queue, uart_buffer, portMAX_DELAY)) {
            uart_write_bytes(UART_NUM_1, uart_buffer, strlen(uart_buffer));
            ESP_LOGI(TAG, "Messagee sent via UART: %s", uart_buffer);
        }
    }
}


void uart_read_task(void *pvParameters){
    char data[UART_BUFFER_SIZE];
    while (1) {
        int len = uart_read_bytes(UART_NUM_1, data, UART_BUFFER_SIZE, pdMS_TO_TICKS(100)); // Czekamy na dane przez 100ms

        if (len > 0) {
            data[len] = '\0'; // Zakończymy łańcuch znaków
            ESP_LOGI(TAG, "Messagee read via UART: %s", data);
            // Szukanie "gr1/wiatrak:"
        if (strstr(data, "gr1_ui/wiatrak:")) {
            if (strstr(data, "gr1_ui/wiatrak: on")) {
                if (xQueueSend(uart_read_queue, "fan_state: on", portMAX_DELAY) != pdPASS) {
                ESP_LOGW("Queue", "Failed to send data to uart_to_mqtt_queue");
            }

            } else if (strstr(data, "gr1_ui/wiatrak: off")) {
                if (xQueueSend(uart_read_queue, "fan_state: off", portMAX_DELAY) != pdPASS) {
                ESP_LOGW("Queue", "Failed to send data to uart_to_mqtt_queue");
            }
            }

        // Szukanie "gr1/swiatlo:"
        } else if (strstr(data, "gr1_ui/swiatlo:")) {
            if (strstr(data, "gr1_ui/swiatlo: on")) {
                if (xQueueSend(uart_read_queue, "light_state: on", portMAX_DELAY) != pdPASS) {
                ESP_LOGW("Queue", "Failed to send data to uart_to_mqtt_queue");
            }
            } else if (strstr(data, "gr1_ui/swiatlo: off")) {
                if (xQueueSend(uart_read_queue, "light_state: off", portMAX_DELAY) != pdPASS) {
                ESP_LOGW("Queue", "Failed to send data to uart_to_mqtt_queue");
            }
            }
        }
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Czekamy na dane
    }
}

void uart_to_udp_task(void *pvParameters) {
    char formatted_message[UART_BUFFER_SIZE];  // Bufor na odebraną wiadomość

    while (1) {
        // Sprawdź, czy są dane w kolejce
        if (xQueueReceive(uart_read_queue, formatted_message, portMAX_DELAY)) {
            char udp_message[128];
            snprintf(udp_message, sizeof(udp_message), formatted_message);
            // Wyślij dane za pomocą UDP
            udp_send_data(udp_message);
            ESP_LOGI(TAG, "Message sent via Thread: %s", udp_message);
        }
    }
}

void app_main(void)
{
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

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
    uart_write_queue = xQueueCreate(UART_QUEUE_SIZE, UART_BUFFER_SIZE);
    if (uart_write_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UART queue");
    }
    uart_read_queue = xQueueCreate(UART_QUEUE_SIZE, UART_BUFFER_SIZE);
    if (uart_read_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UART queue");
    }

    // Tworzenie taska dla OpenThread
    xTaskCreate(ot_task_worker, "ot_task_worker", 4096, NULL, 5, NULL);

    // Tworzenie taska do odbierania danych
    xTaskCreate(udp_to_uart_task, "udp_to_uart_task", 4096, NULL, 5, NULL);
    xTaskCreate(uart_write_task, "uart_write_task", 4096, NULL, 5, NULL);
    xTaskCreate(uart_read_task, "uart_write_task", 4096, NULL, 5, NULL);
    xTaskCreate(uart_to_udp_task, "uart_to_udp_task", 4096, NULL, 5, NULL);

    // Tworzenie taska do monitorowania stanu urządzenia
    xTaskCreate(print_device_status_task, "print_status_task", 2048, NULL, 5, NULL);
}
