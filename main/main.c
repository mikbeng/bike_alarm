#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_crt_bundle.h"

// Pushover credentials
#define PUSHOVER_TOKEN "akm4uombicpg1nhzhhvrvwdjb2qodn"
#define PUSHOVER_USER  "ur8tce4wn5mmpkpt646trnoa962mv1"
#define PUSHOVER_API_ENDPOINT "https://api.pushover.net/1/messages.json"

// WiFi credentials
#define WIFI_SSID "Elysics"
#define WIFI_PASS "ElysicsHemligaRouter"

// Hardware configuration
#define BUTTON_PIN 37  // Botton bin
#define DEBOUNCE_TIME_MS 200

// Add these defines at the top
#define LED_PIN         10
#define LED_BLINK_TIME  500  // Blink every 500ms

static const char *TAG = "pushover_example";


// Add this global variable for the blink task
TaskHandle_t led_task_handle = NULL;

// Global variables for button debouncing
static uint32_t last_button_press = 0;

// Add this function for the LED blink task
void led_blink_task(void* pvParameters) {
    while(1) {
        gpio_set_level(LED_PIN, 1);  // LED on
        vTaskDelay(pdMS_TO_TICKS(LED_BLINK_TIME));
        gpio_set_level(LED_PIN, 0);  // LED off
        vTaskDelay(pdMS_TO_TICKS(LED_BLINK_TIME));
    }
}

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi started, attempting to connect...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi connected!");
                xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 5, &led_task_handle);

                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WiFi disconnected! Attempting to reconnect...");
                // Stop LED blinking when WiFi is disconnected
                if (led_task_handle != NULL) {
                    vTaskDelete(led_task_handle);
                    led_task_handle = NULL;
                    gpio_set_level(LED_PIN, 1);  // Keep LED on solid when disconnected
                }
                esp_wifi_connect();
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// Initialize WiFi
static void wifi_init(void) {
    ESP_LOGI(TAG, "Initializing WiFi...");
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG, "Starting WiFi connection to SSID: %s", WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_start());
}

// Send Pushover notification
static esp_err_t send_pushover_notification(const char* message) {
    ESP_LOGI(TAG, "Preparing to send Pushover notification: %s", message);
    
    esp_http_client_config_t config = {
        .url = PUSHOVER_API_ENDPOINT,
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,    // Use the certificate bundle
        .skip_cert_common_name_check = true,
        .buffer_size = 1024,                          // Increase buffer size
        .buffer_size_tx = 1024                        // Increase transmit buffer size
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    // Prepare POST data
    char post_data[256];
    snprintf(post_data, sizeof(post_data), 
             "token=%s&user=%s&message=%s",
             PUSHOVER_TOKEN, PUSHOVER_USER, message);

    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    ESP_LOGI(TAG, "Sending HTTP request to Pushover...");
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Pushover notification sent successfully (HTTP Status: %d)", status_code);
    } else {
        ESP_LOGE(TAG, "Failed to send Pushover notification, error: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// Button interrupt handler
static void IRAM_ATTR button_isr_handler(void* arg) {
    uint32_t current_time = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
    if ((current_time - last_button_press) > DEBOUNCE_TIME_MS) {
        last_button_press = current_time;
        // Signal main task to send notification
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(arg, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

// Main task
void button_task(void* pvParameters) {
    ESP_LOGI(TAG, "Button task started");
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "Button press detected!");

        send_pushover_notification("Button pressed on ESP32!");

        vTaskDelay(pdMS_TO_TICKS(1000));
        

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting ESP32 Pushover Button Notification System");
    
    // Configure LED GPIO
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);

    // Turn on LED at startup
    gpio_set_level(LED_PIN, 0);

    // Initialize NVS
    ESP_LOGI(TAG, "Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "Erasing NVS flash...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Add this line to enable the certificate bundle
    //ESP_ERROR_CHECK(esp_tls_init_global_ca_store());

    // Initialize WiFi
    wifi_init();

    ESP_LOGI(TAG, "Configuring button GPIO...");
    // Configure button GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    // Create button task
    TaskHandle_t button_task_handle;
    xTaskCreate(button_task, "button_task", 4096, NULL, 10, &button_task_handle);

    // Install GPIO ISR service and add button ISR handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, (void*)button_task_handle);

    ESP_LOGI(TAG, "System initialization complete!");

}