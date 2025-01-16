#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dht.h"
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

#define TAG "firstGroupSensors"
// outputs
#define FAN_LED_GPIO_OUTPUT GPIO_NUM_0 // GPIO0 poki co dioda led zamiast wentylatora
#define LIGHT_GPIO_OUTPUT GPIO_NUM_1 // GPIO1 w którym led symuluje światło

// inputs
#define FAN_SWITCH GPIO_NUM_10 // GPIO10 do przycisku 
#define DHT11_GPIO GPIO_NUM_11 // GPIO11 do DHT11 
#define LIGHT_SWITCH GPIO_NUM_12 // GPIO11 do Światła 

// próg wilgoci po którym załacza się wentylator
#define HUMIDITY_THRESHOLD 45

// Flagi w EventGroup
// #define LED_EVENT_BIT (1 << 0)
#define FLAG_HUMIDITY_HIGH (1 << 0)
#define FLAG_FAN_SWITCH   (1 << 1)
#define FLAG_LIGHT_SWITCH (1 << 2)

// deklaracja grupy flag
static EventGroupHandle_t event_group;

// deklaracje do openthread
static otUdpSocket sUdpSocket;


#define THREAD_BROADCAST_ADDRESS "ff03::1"
#define THREAD_UDP_PORT 12345

typedef struct {
    float temperature;
    float humidity;
    bool fan_state;
    bool light_state;

} sensor_data;

static sensor_data current_data = {0};

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
        ESP_LOGI(TAG, "Message sent via Thread: %s", message);
    }
}

// Callback do odbioru danych
static void udp_receive_callback(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    char buffer[128]; // Bufor na odebrane dane
    int length = otMessageRead(aMessage, otMessageGetOffset(aMessage), buffer, sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0'; // Dodaj zakończenie ciągu znaków
        ESP_LOGI(TAG, "Received message via Thread: %s", buffer);

        EventBits_t flags = xEventGroupGetBits(event_group);
        // Szukanie "light_state"
        if (strstr(buffer, "light_state")) {
            if (strstr(buffer, "light_state: on")) {
                current_data.light_state = 1; // Ustawienie na 1, jeśli "on"
                    xEventGroupSetBits(event_group, FLAG_LIGHT_SWITCH);
            } else if (strstr(buffer, "light_state: off")) {
                xEventGroupClearBits(event_group, FLAG_LIGHT_SWITCH);  // Wyłącz flagę
                current_data.light_state = 0; // Ustawienie na 0, jeśli "off"
            }

        // Szukanie "fan_state"
        } else if (strstr(buffer, "fan_state")) {
            if (strstr(buffer, "fan_state: on")) {
                current_data.fan_state = 1; // Ustawienie na 1, jeśli "on"
                if(!(flags & FLAG_HUMIDITY_HIGH)){
                    xEventGroupSetBits(event_group, FLAG_FAN_SWITCH);
                }
            } else if (strstr(buffer, "fan_state: off")) {
                xEventGroupClearBits(event_group, FLAG_FAN_SWITCH);
                current_data.fan_state = 0; // Ustawienie na 0, jeśli "off"
            }
        }
    } else {
        ESP_LOGW(TAG, "Failed to read UDP message.");
    }
}



// Task do odczytu danych z DHT11
void dht11_task(void *pvParameter){
    int usun = 0;
    static bool last_humidity_high_flag = false; // Zapamiętaj poprzedni stan flagi FLAG_HUMIDITY_HIGH
    static bool current_humidity_high_flag = false;
    while(1){
        usun++; 
        float temperature = 0, humidity = 0;
        

        if (dht_read_float_data(DHT_TYPE_DHT11, DHT11_GPIO, &humidity, &temperature) == ESP_OK) {
            current_data.temperature = temperature;
            current_data.humidity = humidity;

            if (humidity > HUMIDITY_THRESHOLD) {
                xEventGroupSetBits(event_group, FLAG_HUMIDITY_HIGH);
                current_humidity_high_flag = true;

                // Wysyłanie wiadomości przy zmianie FLAG_HUMIDITY_HIGH z 0 na 1
                if (!last_humidity_high_flag & current_humidity_high_flag) {
                    char message[128];
                    snprintf(message, sizeof(message),
                        "{temperature: %.2f, humidity: %.2f}",
                        current_data.temperature, current_data.humidity);
                    udp_send_data(message);
                }
                last_humidity_high_flag = current_humidity_high_flag;
            } else {
                xEventGroupClearBits(event_group, FLAG_HUMIDITY_HIGH);
                current_humidity_high_flag = false;
            }
        }
        if(usun == 10){
            EventBits_t flags = xEventGroupGetBits(event_group);
            usun = 0;
        }
        last_humidity_high_flag = current_humidity_high_flag;
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}

// Task do obsługi przycisku
void fan_switch_task(void *pvParameter) {
    gpio_set_direction(FAN_SWITCH, GPIO_MODE_INPUT);
    gpio_set_pull_mode(FAN_SWITCH, GPIO_PULLDOWN_ONLY);

    static bool last_button_state = false; // Ostatni stan przycisku
    bool current_button_state;             // Aktualny stan przycisku

    while (1) {
        current_button_state = gpio_get_level(FAN_SWITCH);  // Odczytaj stan przycisku

        // Sprawdzenie, czy przycisk został naciśnięty (przechodzi ze stanu LOW na HIGH)
        if (current_button_state && !last_button_state) {

            EventBits_t flags = xEventGroupGetBits(event_group);
            if (flags & FLAG_FAN_SWITCH) {
                xEventGroupClearBits(event_group, FLAG_FAN_SWITCH);  // Wyłącz flagę

            } else {
                xEventGroupSetBits(event_group, FLAG_FAN_SWITCH);  // Włącz flagę
            }
        }

        last_button_state = current_button_state;  // Zapamiętanie stanu przycisku
        vTaskDelay(pdMS_TO_TICKS(30));  // Debouncing (opóźnienie 50ms)
        }
}

// Task do sterowania diodą LED
void fan_task(void *pvParameter) {
    gpio_set_direction(FAN_LED_GPIO_OUTPUT, GPIO_MODE_OUTPUT);
    static bool last_fan_state = false;

    while (1) {
        EventBits_t flags = xEventGroupGetBits(event_group);

        // Tworzenie wiadomości
        char message[128];


        if (flags & FLAG_FAN_SWITCH || flags & FLAG_HUMIDITY_HIGH) {
            current_data.fan_state = true;
            gpio_set_level(FAN_LED_GPIO_OUTPUT, 1); 
        } else {
            current_data.fan_state = false;
            gpio_set_level(FAN_LED_GPIO_OUTPUT, 0);
        }
        if(current_data.fan_state != last_fan_state){
            snprintf(message, sizeof(message),
                    "{fan_state: %d}", current_data.fan_state);
            udp_send_data(message);
            last_fan_state = current_data.fan_state;
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Małe opóźnienie
    }
}

// Task do obsługi wejścia LIGHT_SWITCH
void light_switch_task(void *pvParameter) {
    gpio_set_direction(LIGHT_SWITCH, GPIO_MODE_INPUT);
    gpio_set_pull_mode(LIGHT_SWITCH, GPIO_PULLDOWN_ONLY);

    static bool last_light_input_state = false;
    bool current_light_input_state;

    while (1) {
        current_light_input_state = gpio_get_level(LIGHT_SWITCH);

        if (current_light_input_state && !last_light_input_state) {
            current_data.light_state = current_light_input_state;

            EventBits_t flags = xEventGroupGetBits(event_group);

            if (flags & FLAG_LIGHT_SWITCH) {
                xEventGroupClearBits(event_group, FLAG_LIGHT_SWITCH);
            } else {
                xEventGroupSetBits(event_group, FLAG_LIGHT_SWITCH);
            }
        }

        last_light_input_state = current_light_input_state;
        vTaskDelay(pdMS_TO_TICKS(30)); // Debouncing
    }
}

// Task do sterowania diodą LED (LIGHT_GPIO_OUTPUT)
void light_task(void *pvParameter) {
    gpio_set_direction(LIGHT_GPIO_OUTPUT, GPIO_MODE_OUTPUT);
    static bool last_light_state = false;
    while (1) {
        EventBits_t flags = xEventGroupGetBits(event_group);
        char message[128];

        if (flags & FLAG_LIGHT_SWITCH) {
            current_data.light_state = true;
            gpio_set_level(LIGHT_GPIO_OUTPUT, 1);
        } else {
            current_data.light_state = false;
            gpio_set_level(LIGHT_GPIO_OUTPUT, 0);
        }

        if(current_data.light_state != last_light_state){
            snprintf(message, sizeof(message),
                    "{light_state: %d}", current_data.light_state);
            udp_send_data(message);
            last_light_state = current_data.light_state;
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Małe opóźnienie
    }
}

/////////////////////////////////////////////////
// Konfiguracja sieci

// Funkcja konfigurująca sieć Thread jako End Device
static void configure_ot_network(otInstance *instance) {
    ESP_ERROR_CHECK(esp_openthread_auto_start(NULL)); // Automatyczny start Thread
    ESP_LOGI(TAG, "Thread network configured as End Device.");
}

// Funkcja inicjalizująca OpenThread i sieć
static esp_netif_t *init_ot_netif(const esp_openthread_platform_config_t *config) {
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
    openthread_netif = init_ot_netif(&config);
    esp_netif_set_default_netif(openthread_netif);

    // Konfiguracja sieci jako urządzenie końcowe
    configure_ot_network(esp_openthread_get_instance());

    // Uruchom główną pętlę OpenThread
    esp_openthread_launch_mainloop();

    // Sprzątanie (nie będzie wywoływane w normalnym scenariuszu)
    esp_openthread_netif_glue_deinit();
    esp_netif_destroy(openthread_netif);
    esp_vfs_eventfd_unregister();
    vTaskDelete(NULL);
}

// Task do wypisywania statusu urządzenia co 5 sekund
static void device_status_task(void *arg)
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
        vTaskDelay(pdMS_TO_TICKS(120000)); // Co 5 sekund
    }
}


////////////////////////////
// Wysylanie danych po UDP

// Funkcja do inicjalizacji gniazda UDP
void initUdp(void) {
    otSockAddr dest_addr;
    otError error;
    otInstance *sInstance;

    sInstance = esp_openthread_get_instance();
    if (sInstance == NULL) {
        ESP_LOGE(TAG, "OpenThread instance not initialized.");
        return;
    }

    // Ustawienia gniazda
    memset(&sUdpSocket, 0, sizeof(sUdpSocket));
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.mPort = THREAD_UDP_PORT;  // Przykładowy port odbiorcy
    // otIp6AddressFromString("fd00::1", &dest_addr.mAddress); // Adres urządzenia odbierającego
    otIp6AddressFromString("::", &dest_addr.mAddress);


    // Tworzenie gniazda UDP
    error = otUdpOpen(sInstance, &sUdpSocket, udp_receive_callback, NULL);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to open UDP socket, error: %d", error);
        return;
    }

    // Przypisanie portu
    error = otUdpBind(sInstance, &sUdpSocket, &dest_addr, OT_NETIF_THREAD);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to bind UDP socket, error: %d", error);
        return;
    }

    ESP_LOGI(TAG, "UDP socket initialized.");
}



// Task do wysyłania danych po UDP
void udp_send_task(void *pvParameter) {
    initUdp(); // Inicjalizacja gniazda UDP

    while (1) {
        otInstance *sInstance = esp_openthread_get_instance();
        // Tworzenie wiadomości
        char message[128];
        snprintf(message, sizeof(message),
                 "{temperature: %.2f, humidity: %.2f}",
                 current_data.temperature, current_data.humidity);

        // Wysyłanie wiadomości
        udp_send_data(message);
        vTaskDelay(pdMS_TO_TICKS(120000)); // Wysyłanie co 120 sekundy
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

    // Tworzenie taska do konfiguracji OpenThread
    xTaskCreate(ot_task_worker, "ot_task_worker", 4096, NULL, 5, NULL);

    // Tworzenie taska do monitorowania stanu urządzenia
    xTaskCreate(device_status_task, "print_status_task", 2048, NULL, 5, NULL);
    

    event_group = xEventGroupCreate();
    
    // Tworzenie tasków
    xTaskCreate(&dht11_task, "DHT11 Task", 2048, NULL, 5, NULL);
    xTaskCreate(&fan_switch_task, "Button Task", 2048, NULL, 5, NULL);
    xTaskCreate(&fan_task, "LED Task", 2048, NULL, 5, NULL);
    xTaskCreate(&light_switch_task, "Light Input Task", 2048, NULL, 5, NULL);
    xTaskCreate(&light_task, "Light Output Task", 2048, NULL, 5, NULL);

    xTaskCreate(udp_send_task, "udp_send_task", 8192, NULL, 5, NULL);

}