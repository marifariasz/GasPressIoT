/* AULA IoT - Ricardo Prates - 001 - Cliente MQTT - Publisher:/pressure,/gas; Subscribed:/led
 *
 * Material de suporte - 27/05/2025
 * 
 * Código adaptado de: https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/mqtt 
 */

#include "pico/stdlib.h"            // Biblioteca da Raspberry Pi Pico para funções padrão
#include "pico/cyw43_arch.h"        // Biblioteca para Wi-Fi da Pico com CYW43
#include "pico/unique_id.h"         // Biblioteca para identificador único da placa
#include <math.h>                   // Biblioteca para funções matemáticas (fabs)

#include "hardware/gpio.h"          // Biblioteca de hardware de GPIO
#include "hardware/irq.h"           // Biblioteca de interrupções
#include "hardware/adc.h"           // Biblioteca para conversão ADC
#include "hardware/pwm.h"           // Biblioteca para PWM

#include "lwip/apps/mqtt.h"         // Biblioteca LWIP MQTT
#include "lwip/apps/mqtt_priv.h"    // Funções para conexões MQTT
#include "lwip/dns.h"               // Suporte DNS
#include "lwip/altcp_tls.h"         // Conexões seguras com TLS

#define WIFI_SSID "TIM_ULTRAFIBRA_28A0"                  // Substitua pelo nome da sua rede Wi-Fi
#define WIFI_PASSWORD "64t4fu76eb"      // Substitua pela senha da sua rede Wi-Fi
#define MQTT_SERVER "192.168.1.9"               // Endereço do broker MQTT
#define MQTT_USERNAME "mariana"                    // Usuário do broker MQTT
#define MQTT_PASSWORD "mariana"                    // Senha do broker MQTT

#define EIXO_Y 26              // Pino ADC0 para pressão (eixo Y do joystick)
#define EIXO_X 27              // Pino ADC1 para gás (eixo X do joystick)
#define LED_PIN 13             // Pino para LED vermelho externo
#define BUZZER_PIN 21          // Pino para buzzer ativo

// Escala de temperatura
#ifndef TEMPERATURE_UNITS
#define TEMPERATURE_UNITS 'C' // 'F' para Fahrenheit
#endif

#ifndef MQTT_SERVER
#error Need to define MQTT_SERVER
#endif

#ifndef MQTT_TOPIC_LEN
#define MQTT_TOPIC_LEN 100
#endif

// Dados do cliente MQTT
typedef struct {
    mqtt_client_t* mqtt_client_inst;
    struct mqtt_connect_client_info_t mqtt_client_info;
    char data[MQTT_OUTPUT_RINGBUF_SIZE];
    char topic[MQTT_TOPIC_LEN];
    uint32_t len;
    ip_addr_t mqtt_server_address;
    bool connect_done;
    int subscribe_count;
    bool stop_client;
    bool led_state; // Estado atual do LED
} MQTT_CLIENT_DATA_T;

#ifndef DEBUG_printf
#ifndef NDEBUG
#define DEBUG_printf printf
#else
#define DEBUG_printf(...)
#endif
#endif

#ifndef INFO_printf
#define INFO_printf printf
#endif

#ifndef ERROR_printf
#define ERROR_printf printf
#endif

#define TEMP_WORKER_TIME_S 2 // Atualização a cada 2 segundos
#define MQTT_KEEP_ALIVE_S 60
#define MQTT_SUBSCRIBE_QOS 1
#define MQTT_PUBLISH_QOS 1
#define MQTT_PUBLISH_RETAIN 0
#define MQTT_WILL_TOPIC "/online"
#define MQTT_WILL_MSG "0"
#define MQTT_WILL_QOS 1
#ifndef MQTT_DEVICE_NAME
#define MQTT_DEVICE_NAME "pico"
#endif
#ifndef MQTT_UNIQUE_TOPIC
#define MQTT_UNIQUE_TOPIC 0
#endif

#define BUZZER_FREQ 1000       // Frequência do PWM para o buzzer (1000 Hz)
#define BUZZER_DUTY_CYCLE 50   // Ciclo de trabalho do PWM (50%)
#define BUZZER_INTERVAL_MS 500 // Intervalo intermitente (500 ms ligado/desligado)

static float read_onboard_pressure(const char unit);
static float read_onboard_gas(const char unit);
static void pub_request_cb(__unused void *arg, err_t err);
static const char *full_topic(MQTT_CLIENT_DATA_T *state, const char *name);
static void control_led(MQTT_CLIENT_DATA_T *state, bool on);
static void publish_pressure(MQTT_CLIENT_DATA_T *state);
static void publish_gas(MQTT_CLIENT_DATA_T *state);
static void sub_request_cb(void *arg, err_t err);
static void unsub_request_cb(void *arg, err_t err);
static void sub_unsub_topics(MQTT_CLIENT_DATA_T* state, bool sub);
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags);
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len);
static void pressure_worker_fn(async_context_t *context, async_at_time_worker_t *worker);
static async_at_time_worker_t pressure_worker = { .do_work = pressure_worker_fn };
static void gas_worker_fn(async_context_t *context, async_at_time_worker_t *worker);
static async_at_time_worker_t gas_worker = { .do_work = gas_worker_fn };
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status);
static void start_client(MQTT_CLIENT_DATA_T *state);
static void dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg);

int main(void) {
    stdio_init_all();
    INFO_printf("mqtt client starting\n");

    adc_init();
    adc_gpio_init(EIXO_Y);
    adc_gpio_init(EIXO_X);

    // Inicializa o pino do LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_disable_pulls(LED_PIN); // Desativa pull-up/pull-down
    gpio_put(LED_PIN, 0); // LED inicialmente desligado

    // Inicializa o pino do buzzer com PWM
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_config config = pwm_get_default_config();
    // Configura a frequência do PWM (~1000 Hz)
    float divider = 125000000.0f / (BUZZER_FREQ * 65535); // Clock do Pico = 125 MHz
    pwm_config_set_clkdiv(&config, divider);
    pwm_config_set_wrap(&config, 65535); // Resolução de 16 bits
    pwm_init(slice_num, &config, true);
    pwm_set_gpio_level(BUZZER_PIN, 0); // Buzzer inicialmente desligado

    static MQTT_CLIENT_DATA_T state = { .led_state = false }; // Inicializa LED como desligado

    if (cyw43_arch_init()) {
        panic("Failed to initialize CYW43");
    }

    char unique_id_buf[5];
    pico_get_unique_board_id_string(unique_id_buf, sizeof(unique_id_buf));
    for(int i=0; i < sizeof(unique_id_buf) - 1; i++) {
        unique_id_buf[i] = tolower(unique_id_buf[i]);
    }

    char client_id_buf[sizeof(MQTT_DEVICE_NAME) + sizeof(unique_id_buf)];
    memcpy(&client_id_buf[0], MQTT_DEVICE_NAME, sizeof(MQTT_DEVICE_NAME) - 1);
    memcpy(&client_id_buf[sizeof(MQTT_DEVICE_NAME) - 1], unique_id_buf, sizeof(unique_id_buf));
    client_id_buf[sizeof(client_id_buf) - 1] = '\0';
    INFO_printf("Device name: %s\n", client_id_buf);

    state.mqtt_client_info.client_id = client_id_buf;
    state.mqtt_client_info.keep_alive = MQTT_KEEP_ALIVE_S;
#if defined(MQTT_USERNAME) && defined(MQTT_PASSWORD)
    state.mqtt_client_info.client_user = MQTT_USERNAME;
    state.mqtt_client_info.client_pass = MQTT_PASSWORD;
#else
    state.mqtt_client_info.client_user = NULL;
    state.mqtt_client_info.client_pass = NULL;
#endif
    static char will_topic[MQTT_TOPIC_LEN];
    strncpy(will_topic, full_topic(&state, MQTT_WILL_TOPIC), sizeof(will_topic));
    state.mqtt_client_info.will_topic = will_topic;
    state.mqtt_client_info.will_msg = MQTT_WILL_MSG;
    state.mqtt_client_info.will_qos = MQTT_WILL_QOS;
    state.mqtt_client_info.will_retain = true;

#if LWIP_ALTCP && LWIP_ALTCP_TLS
#ifdef MQTT_CERT_INC
    static const uint8_t ca_cert[] = TLS_ROOT_CERT;
    static const uint8_t client_key[] = TLS_CLIENT_KEY;
    static const uint8_t client_cert[] = TLS_CLIENT_CERT;
    state.mqtt_client_info.tls_config = altcp_tls_create_config_client_2wayauth(ca_cert, sizeof(ca_cert),
            client_key, sizeof(client_key), NULL, 0, client_cert, sizeof(client_cert));
#if ALTCP_MBEDTLS_AUTHMODE != MBEDTLS_SSL_VERIFY_REQUIRED
    WARN_printf("Warning: tls without verification is insecure\n");
#endif
#else
    state.mqtt_client_info.tls_config = altcp_tls_create_config_client(NULL, 0);
    WARN_printf("Warning: tls without a certificate is insecure\n");
#endif
#endif

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        panic("Failed to connect");
    }
    INFO_printf("\nConnected to Wifi\n");

    cyw43_arch_lwip_begin();
    int err = dns_gethostbyname(MQTT_SERVER, &state.mqtt_server_address, dns_found, &state);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        start_client(&state);
    } else if (err != ERR_INPROGRESS) {
        panic("dns request failed");
    }

    while (!state.connect_done || mqtt_client_is_connected(state.mqtt_client_inst)) {
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10000));
    }

    INFO_printf("mqtt client exiting\n");
    return 0;
}

static float read_onboard_pressure(const char unit) {
    adc_select_input(0);
    uint16_t raw_value = adc_read(); // Lê valor ADC (0 a 4095)
    float pressure_percent = (raw_value / 4095.0f) * 100.0f; // Converte para porcentagem
    INFO_printf("Pressure: Raw ADC=%u, Percent=%.2f%%\n", raw_value, pressure_percent);
    return pressure_percent;
}

static float read_onboard_gas(const char unit) {
    adc_select_input(1);
    uint32_t adc_channel = adc_get_selected_input();
    if (adc_channel != 1) {
        ERROR_printf("ADC channel mismatch: expected 1, got %u\n", adc_channel);
    }
    uint16_t raw_value = adc_read();
    float voltage = (raw_value / 4095.0f) * 3.3f;
    float gas_concentration = voltage * 100.0f; // Escala original
    float gas_percent = (gas_concentration / 330.0f) * 100.0f; // 330 é 100%
    INFO_printf("Gas: Raw ADC=%u, Voltage=%.3fV, Gas=%.2f, Percent=%.2f%%\n", 
                raw_value, voltage, gas_concentration, gas_percent);
    return gas_percent;
}

static void pub_request_cb(__unused void *arg, err_t err) {
    if (err != 0) {
        ERROR_printf("pub_request_cb failed %d\n", err);
    } else {
        INFO_printf("MQTT publish successful\n");
    }
}

static const char *full_topic(MQTT_CLIENT_DATA_T *state, const char *name) {
#if MQTT_UNIQUE_TOPIC
    static char full_topic[MQTT_TOPIC_LEN];
    snprintf(full_topic, sizeof(full_topic), "/%s%s", state->mqtt_client_info.client_id, name);
    INFO_printf("Using topic: %s\n", full_topic);
    return full_topic;
#else
    INFO_printf("Using topic: %s\n", name);
    return name;
#endif
}

static void control_led(MQTT_CLIENT_DATA_T *state, bool on) {
    static absolute_time_t last_buzzer_toggle = {0};
    static bool buzzer_on = false;
    
    if (state->led_state != on) { // Atualiza apenas se o estado mudar
        state->led_state = on;
        const char* message = on ? "On" : "Off";
        gpio_put(LED_PIN, on ? 1 : 0);
        bool pin_state = gpio_get(LED_PIN);
        INFO_printf("Setting LED on pin %d to %s (actual pin state: %d)\n", LED_PIN, message, pin_state);
        
        if (!on) {
            pwm_set_gpio_level(BUZZER_PIN, 0); // Desliga o buzzer
            buzzer_on = false;
            INFO_printf("Buzzer PWM off\n");
        } else {
            last_buzzer_toggle = get_absolute_time(); // Reseta o temporizador
        }

        if (mqtt_client_is_connected(state->mqtt_client_inst)) {
            mqtt_publish(state->mqtt_client_inst, full_topic(state, "/led"), message, strlen(message), 
                         MQTT_PUBLISH_QOS, MQTT_PUBLISH_RETAIN, pub_request_cb, state);
            INFO_printf("Published LED %s to %s\n", message, full_topic(state, "/led"));
        } else {
            ERROR_printf("Cannot publish to /led: MQTT client not connected\n");
        }
    }

    // Controla o buzzer intermitente com PWM quando o LED está ligado
    if (state->led_state) {
        if (absolute_time_diff_us(last_buzzer_toggle, get_absolute_time()) >= BUZZER_INTERVAL_MS * 1000) {
            buzzer_on = !buzzer_on;
            pwm_set_gpio_level(BUZZER_PIN, buzzer_on ? (65535 * BUZZER_DUTY_CYCLE / 100) : 0);
            INFO_printf("Buzzer PWM %s\n", buzzer_on ? "on" : "off");
            last_buzzer_toggle = get_absolute_time();
        }
    }
}

static void publish_pressure(MQTT_CLIENT_DATA_T *state) {
    static float old_pressure = -1.0f;
    const char *pressure_key = full_topic(state, "/pressure");
    float pressure = read_onboard_pressure(TEMPERATURE_UNITS);
    bool led_on = (pressure > 60.0f) || (read_onboard_gas(TEMPERATURE_UNITS) > 40.0f);
    control_led(state, led_on);
    if (fabs(pressure - old_pressure) > 0.1f || old_pressure == -1.0f) {
        old_pressure = pressure;
        char temp_str[16];
        snprintf(temp_str, sizeof(temp_str), "%.2f", pressure);
        INFO_printf("Publishing %s to %s\n", temp_str, pressure_key);
        if (mqtt_client_is_connected(state->mqtt_client_inst)) {
            mqtt_publish(state->mqtt_client_inst, pressure_key, temp_str, strlen(temp_str), 
                         MQTT_PUBLISH_QOS, MQTT_PUBLISH_RETAIN, pub_request_cb, state);
        } else {
            ERROR_printf("Cannot publish to %s: MQTT client not connected\n", pressure_key);
        }
    }
    // Publica estado atual do LED a cada chamada
    if (mqtt_client_is_connected(state->mqtt_client_inst)) {
        const char* led_message = state->led_state ? "On" : "Off";
        mqtt_publish(state->mqtt_client_inst, full_topic(state, "/led"), led_message, strlen(led_message), 
                     MQTT_PUBLISH_QOS, MQTT_PUBLISH_RETAIN, pub_request_cb, state);
        INFO_printf("Published LED %s to %s (periodic update)\n", led_message, full_topic(state, "/led"));
    }
}

static void publish_gas(MQTT_CLIENT_DATA_T *state) {
    static float old_gas = -1.0f;
    const char *gas_key = full_topic(state, "/gas");
    float gas = read_onboard_gas(TEMPERATURE_UNITS);
    bool led_on = (gas > 50.0f) || (read_onboard_pressure(TEMPERATURE_UNITS) > 60.0f);
    control_led(state, led_on);
    if (fabs(gas - old_gas) > 0.1f || old_gas == -1.0f) {
        old_gas = gas;
        char temp_str[16];
        snprintf(temp_str, sizeof(temp_str), "%.2f", gas);
        INFO_printf("Publishing gas: %s to %s\n", temp_str, gas_key);
        if (mqtt_client_is_connected(state->mqtt_client_inst)) {
            mqtt_publish(state->mqtt_client_inst, gas_key, temp_str, strlen(temp_str), 
                         MQTT_PUBLISH_QOS, MQTT_PUBLISH_RETAIN, pub_request_cb, state);
        } else {
            ERROR_printf("Cannot publish to %s: MQTT client not connected\n", gas_key);
        }
    }
    // Publica estado atual do LED
    if (mqtt_client_is_connected(state->mqtt_client_inst)) {
        const char* led_message = state->led_state ? "On" : "Off";
        mqtt_publish(state->mqtt_client_inst, full_topic(state, "/led"), led_message, strlen(led_message), 
                     MQTT_PUBLISH_QOS, MQTT_PUBLISH_RETAIN, pub_request_cb, state);
        INFO_printf("Published LED %s to %s (periodic update)\n", led_message, full_topic(state, "/led"));
    }
}

static void sub_request_cb(void *arg, err_t err) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)arg;
    if (err != 0) {
        ERROR_printf("subscribe request failed %d\n", err);
    } else {
        INFO_printf("Subscribed successfully\n");
    }
    state->subscribe_count++;
}

static void unsub_request_cb(void *arg, err_t err) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)arg;
    if (err != 0) {
        ERROR_printf("unsubscribe request failed %d\n", err);
    }
    state->subscribe_count--;
    if (state->subscribe_count <= 0 && state->stop_client) {
        mqtt_disconnect(state->mqtt_client_inst);
    }
}

static void sub_unsub_topics(MQTT_CLIENT_DATA_T* state, bool sub) {
    mqtt_request_cb_t cb = sub ? sub_request_cb : unsub_request_cb;
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/led"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/print"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/ping"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
    mqtt_sub_unsub(state->mqtt_client_inst, full_topic(state, "/exit"), MQTT_SUBSCRIBE_QOS, cb, state, sub);
}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)arg;
#if MQTT_UNIQUE_TOPIC
    const char *basic_topic = state->topic + strlen(state->mqtt_client_info.client_id) + 1;
#else
    const char *basic_topic = state->topic;
#endif
    strncpy(state->data, (const char *)data, len);
    state->data[len] = '\0';

    DEBUG_printf("Topic: %s, Message: %s\n", state->topic, state->data);
    if (strcmp(basic_topic, "/led") == 0) {
        if (lwip_stricmp((const char *)state->data, "On") == 0 || strcmp((const char *)state->data, "1") == 0) {
            control_led(state, true);
        } else if (lwip_stricmp((const char *)state->data, "Off") == 0 || strcmp((const char *)state->data, "0") == 0) {
            control_led(state, false);
        }
    } else if (strcmp(basic_topic, "/print") == 0) {
        INFO_printf("%.*s\n", len, state->data);
    } else if (strcmp(basic_topic, "/ping") == 0) {
        char buf[11];
        snprintf(buf, sizeof(buf), "%u", to_ms_since_boot(get_absolute_time()) / 1000);
        if (mqtt_client_is_connected(state->mqtt_client_inst)) {
            mqtt_publish(state->mqtt_client_inst, full_topic(state, "/uptime"), buf, strlen(buf), 
                     MQTT_PUBLISH_QOS, MQTT_PUBLISH_RETAIN, pub_request_cb, state);
        }
    } else if (strcmp(basic_topic, "/exit") == 0) {
        state->stop_client = true;
        sub_unsub_topics(state, false);
    }
}

static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)arg;
    strncpy(state->topic, topic, sizeof(state->topic));
}

static void pressure_worker_fn(async_context_t *context, async_at_time_worker_t *worker) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)worker->user_data;
    publish_pressure(state);
    async_context_add_at_time_worker_in_ms(cyw43_arch_async_context(), worker, TEMP_WORKER_TIME_S * 1000);
}

static void gas_worker_fn(async_context_t *context, async_at_time_worker_t *worker) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)worker->user_data;
    publish_gas(state);
    async_context_add_at_time_worker_in_ms(context, worker, TEMP_WORKER_TIME_S * 1000);
}

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    MQTT_CLIENT_DATA_T* state = (MQTT_CLIENT_DATA_T*)arg;
    if (status == MQTT_CONNECT_ACCEPTED) {
        state->connect_done = true;
        INFO_printf("Connected to MQTT broker\n");
        // Limpa mensagem retida em /led
        if (mqtt_client_is_connected(state->mqtt_client_inst)) {
            mqtt_publish(state->mqtt_client_inst, full_topic(state, "/led"), "", 0, 
                MQTT_PUBLISH_QOS, true, pub_request_cb, state);
            INFO_printf("Cleared retained message on %s\n", full_topic(state, "/led"));
        }
        sub_unsub_topics(state, true);
        if (state->mqtt_client_info.will_topic) {
            mqtt_publish(state->mqtt_client_inst, state->mqtt_client_info.will_topic, "1", 1, 
                         MQTT_WILL_QOS, true, pub_request_cb, state);
        }
        pressure_worker.user_data = state;
        gas_worker.user_data = state;
        async_context_add_at_time_worker_in_ms(cyw43_arch_async_context(), &pressure_worker, 0);
        async_context_add_at_time_worker_in_ms(cyw43_arch_async_context(), &gas_worker, 0);
        control_led(state, false); // Inicializa LED como desligado
    } else if (status == MQTT_CONNECT_DISCONNECTED) {
        if (!state->connect_done) {
            ERROR_printf("Failed to connect to mqtt server\n");
        }
    } else {
        ERROR_printf("Unexpected MQTT status: %d\n", status);
    }
}

static void start_client(MQTT_CLIENT_DATA_T *state) {
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    const int port = MQTT_TLS_PORT;
    INFO_printf("Using TLS\n");
#else
    const int port = MQTT_PORT;
    INFO_printf("Warning: Not using TLS\n");
#endif

    state->mqtt_client_inst = mqtt_client_new();
    if (!state->mqtt_client_inst) {
        panic("MQTT client instance creation error");
    }
    INFO_printf("IP address of this device %s\n", ipaddr_ntoa(&(netif_list->ip_addr)));
    INFO_printf("Connecting to mqtt server at %s\n", ipaddr_ntoa(&state->mqtt_server_address));

    cyw43_arch_lwip_begin();
    if (mqtt_client_connect(state->mqtt_client_inst, &state->mqtt_server_address, port, 
                           mqtt_connection_cb, state, &state->mqtt_client_info) != ERR_OK) {
        panic("MQTT broker connection error");
    }
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    mbedtls_ssl_set_hostname(altcp_tls_context(state->mqtt_client_inst->conn), MQTT_SERVER);
#endif
    mqtt_set_inpub_callback(state->mqtt_client_inst, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, state);
    cyw43_arch_lwip_end();
}

static void dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    MQTT_CLIENT_DATA_T *state = (MQTT_CLIENT_DATA_T*)arg;
    if (ipaddr) {
        state->mqtt_server_address = *ipaddr;
        start_client(state);
    } else {
        panic("dns request failed");
    }
}
