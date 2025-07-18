/*
 * Iperf example - wifi commands
 *
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "iperf.h"

#include "wifi_conf.h"
//#include "my_http.h"
#include "my_mqtt.h"
typedef struct {
    struct arg_str *ip;
    struct arg_lit *server;
    struct arg_lit *udp;
    struct arg_lit *version;
    struct arg_int *port;
    struct arg_int *length;
    struct arg_int *interval;
    struct arg_int *time;
    struct arg_lit *abort;
    struct arg_end *end;
} wifi_iperf_t;
static wifi_iperf_t iperf_args;

typedef struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_args_t;

typedef struct {
    struct arg_str *ssid;
    struct arg_end *end;
} wifi_scan_arg_t;

static wifi_args_t sta_args;
static wifi_scan_arg_t scan_args;
static wifi_args_t ap_args;

/*my code*/
typedef struct {
    struct arg_str *value;
    struct arg_end *end;
} wifi_send_arg_t;
static wifi_send_arg_t wifi_send_args;
 
//

static bool reconnect = true;
static const char *TAG = "iperf";

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
const int DISCONNECTED_BIT = BIT1;
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

static void scan_done_handler(void)
{
    uint16_t sta_number = 0;
    uint8_t i;
    wifi_ap_record_t *ap_list_buffer;

    esp_wifi_scan_get_ap_num(&sta_number);
    ap_list_buffer = malloc(sta_number * sizeof(wifi_ap_record_t));
    if (ap_list_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to malloc buffer to print scan results");
        esp_wifi_clear_ap_list();
        return;
    }

    if (esp_wifi_scan_get_ap_records(&sta_number, (wifi_ap_record_t *)ap_list_buffer) == ESP_OK) {
        for (i = 0; i < sta_number; i++) {
            ESP_LOGI(TAG, "[%s][rssi=%d]", ap_list_buffer[i].ssid, ap_list_buffer[i].rssi);
        }
    }
    free(ap_list_buffer);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
    case WIFI_EVENT_SCAN_DONE:
        scan_done_handler();
        ESP_LOGI(TAG, "sta scan done");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "L2 connected");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        if (reconnect) {
            ESP_LOGI(TAG, "sta disconnect, reconnect...");
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "sta disconnect");
        }
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, DISCONNECTED_BIT);
        break;
    default:
        break;
    }
    return;
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    switch (event_id) {
    case IP_EVENT_STA_GOT_IP:
        xEventGroupClearBits(wifi_event_group, DISCONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        ESP_LOGI(TAG, "got ip");
        break;
    default:
        break;
    }
    return;
}

void initialise_wifi(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    static bool initialized = false;

    if (initialized) {
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_ps(WIFI_PS_MIN_MODEM) ); //must call this
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    initialized = true;
}

static bool wifi_cmd_sta_join(const char *ssid, const char *pass)
{
    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);

    wifi_config_t wifi_config = { 0 };

    strlcpy((char *) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strncpy((char *) wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }

    if (bits & CONNECTED_BIT) {
        reconnect = false;
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        xEventGroupWaitBits(wifi_event_group, DISCONNECTED_BIT, 0, 1, portTICK_PERIOD_MS);
    }

    reconnect = true;
    esp_wifi_disconnect();
    //ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) ); //by snake
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    esp_wifi_connect();

    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 5000 / portTICK_PERIOD_MS);

    return true;
}

static int wifi_cmd_sta(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &sta_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, sta_args.end, argv[0]);
        return 1;
    }
    
    const char *ssid = (sta_args.ssid->count > 0) ? sta_args.ssid->sval[0] : DEFAULT_SSID;
    const char *password = (sta_args.password->count > 0) ? sta_args.password->sval[0] : DEFAULT_PASSWORD;

    ESP_LOGI(TAG, "sta connecting to '%s'", ssid);
    wifi_cmd_sta_join(ssid, password);
    return 0;
}

static bool wifi_cmd_sta_scan(const char *ssid)
{
    wifi_scan_config_t scan_config = { 0 };
    scan_config.ssid = (uint8_t *) ssid;

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    esp_wifi_scan_start(&scan_config, false);

    return true;
}

static int wifi_cmd_scan(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &scan_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, scan_args.end, argv[0]);
        return 1;
    }

    ESP_LOGI(TAG, "sta start to scan");
    if ( scan_args.ssid->count == 1 ) {
        wifi_cmd_sta_scan(scan_args.ssid->sval[0]);
    } else {
        wifi_cmd_sta_scan(NULL);
    }
    return 0;
}


static bool wifi_cmd_ap_set(const char *ssid, const char *pass)
{
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "",
            .ssid_len = 0,
            .max_connection = 4,
            .password = "",
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    reconnect = false;
    strncpy((char *) wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
    if (pass) {
        if (strlen(pass) != 0 && strlen(pass) < 8) {
            reconnect = true;
            ESP_LOGE(TAG, "password less than 8");
            return false;
        }
        strncpy((char *) wifi_config.ap.password, pass, sizeof(wifi_config.ap.password));
    }

    if (strlen(pass) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    return true;
}

static int wifi_cmd_ap(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &ap_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, ap_args.end, argv[0]);
        return 1;
    }

    wifi_cmd_ap_set(ap_args.ssid->sval[0], ap_args.password->sval[0]);
    ESP_LOGI(TAG, "AP mode, %s %s", ap_args.ssid->sval[0], ap_args.password->sval[0]);
    return 0;
}

static int wifi_cmd_query(int argc, char **argv)
{
    wifi_config_t cfg;
    wifi_mode_t mode;

    esp_wifi_get_mode(&mode);
    if (WIFI_MODE_AP == mode) {
        esp_wifi_get_config(WIFI_IF_AP, &cfg);
        ESP_LOGI(TAG, "AP mode, %s %s", cfg.ap.ssid, cfg.ap.password);
    } else if (WIFI_MODE_STA == mode) {
        int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
        if (bits & CONNECTED_BIT) {
            esp_wifi_get_config(WIFI_IF_STA, &cfg);
            ESP_LOGI(TAG, "sta mode, connected %s", cfg.ap.ssid);
        } else {
            ESP_LOGI(TAG, "sta mode, disconnected");
        }
    } else {
        ESP_LOGI(TAG, "NULL mode");
        return 0;
    }

    return 0;
}

static uint32_t wifi_get_local_ip(void)
{
    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
    esp_netif_t *ifx = ap_netif;
    esp_netif_ip_info_t ip_info;
    wifi_mode_t mode;

    esp_wifi_get_mode(&mode);
    if (WIFI_MODE_STA == mode) {
        bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
        if (bits & CONNECTED_BIT) {
            ifx = sta_netif;
        } else {
            ESP_LOGE(TAG, "sta has no IP");
            return 0;
        }
    }

    esp_netif_get_ip_info(ifx, &ip_info);
    return ip_info.ip.addr;
}

static int wifi_cmd_iperf(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &iperf_args);
    iperf_cfg_t cfg;

    if (nerrors != 0) {
        arg_print_errors(stderr, iperf_args.end, argv[0]);
        return 0;
    }

    memset(&cfg, 0, sizeof(cfg));

    // wifi iperf only support IPV4 address
    cfg.type = IPERF_IP_TYPE_IPV4;

    if ( iperf_args.abort->count != 0) {
        iperf_stop();
        return 0;
    }

    if ( ((iperf_args.ip->count == 0) && (iperf_args.server->count == 0)) ||
            ((iperf_args.ip->count != 0) && (iperf_args.server->count != 0)) ) {
        ESP_LOGE(TAG, "should specific client/server mode");
        return 0;
    }

    if (iperf_args.ip->count == 0) {
        cfg.flag |= IPERF_FLAG_SERVER;
    } else {
        cfg.destination_ip4 = esp_ip4addr_aton(iperf_args.ip->sval[0]);
        cfg.flag |= IPERF_FLAG_CLIENT;
    }

    cfg.source_ip4 = wifi_get_local_ip();
    if (cfg.source_ip4 == 0) {
        return 0;
    }

    if (iperf_args.length->count == 0) {
        cfg.len_send_buf = 0;
    } else {
        cfg.len_send_buf = iperf_args.length->ival[0];
    }

    if (iperf_args.udp->count == 0) {
        cfg.flag |= IPERF_FLAG_TCP;
    } else {
        cfg.flag |= IPERF_FLAG_UDP;
    }

    if (iperf_args.port->count == 0) {
        cfg.sport = IPERF_DEFAULT_PORT;
        cfg.dport = IPERF_DEFAULT_PORT;
    } else {
        if (cfg.flag & IPERF_FLAG_SERVER) {
            cfg.sport = iperf_args.port->ival[0];
            cfg.dport = IPERF_DEFAULT_PORT;
        } else {
            cfg.sport = IPERF_DEFAULT_PORT;
            cfg.dport = iperf_args.port->ival[0];
        }
    }

    if (iperf_args.interval->count == 0) {
        cfg.interval = IPERF_DEFAULT_INTERVAL;
    } else {
        cfg.interval = iperf_args.interval->ival[0];
        if (cfg.interval <= 0) {
            cfg.interval = IPERF_DEFAULT_INTERVAL;
        }
    }

    if (iperf_args.time->count == 0) {
        cfg.time = IPERF_DEFAULT_TIME;
    } else {
        cfg.time = iperf_args.time->ival[0];
        if (cfg.time <= cfg.interval) {
            cfg.time = cfg.interval;
        }
    }

    ESP_LOGI(TAG, "mode=%s-%s sip=%" PRIu32 ".%" PRIu32 ".%" PRIu32 ".%" PRIu32 ":%d, \
             dip=%" PRIu32 ".%" PRIu32 ".%" PRIu32 ".%" PRIu32 ":%d, interval=%" PRIu32 ", time=%" PRIu32,
             cfg.flag & IPERF_FLAG_TCP ? "tcp" : "udp",
             cfg.flag & IPERF_FLAG_SERVER ? "server" : "client",
             cfg.source_ip4 & 0xFF, (cfg.source_ip4 >> 8) & 0xFF, (cfg.source_ip4 >> 16) & 0xFF,
             (cfg.source_ip4 >> 24) & 0xFF, cfg.sport,
             cfg.destination_ip4 & 0xFF, (cfg.destination_ip4 >> 8) & 0xFF, (cfg.destination_ip4 >> 16) & 0xFF,
             (cfg.destination_ip4 >> 24) & 0xFF, cfg.dport,
             cfg.interval, cfg.time);

    iperf_start(&cfg);

    return 0;
}

static int restart(int argc, char **argv)
{
    ESP_LOGI(TAG, "Restarting");
    esp_restart();
}

static int heap_size(int argc, char **argv)
{
    uint32_t heap_size = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "min heap size: %" PRIu32, heap_size);
    return 0;
}

// my code
//extern TaskHandle_t http_task_handle;
extern TaskHandle_t mqtt_task_handle;
extern bool send_wifi;

int check_wifi_conn() {
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK) {
        ESP_LOGI("WiFi", "Connected to SSID: %s", ap_info.ssid);
        return 1;
    } else {
        ESP_LOGW("WiFi", "Not connected to any AP");
        return 0;
    }
}

int start_send_wifi_task(int argc, char **argv){
	
	if (!check_wifi_conn()){
		return 1;
	}
	
	ESP_LOGI(TAG, "start_send_wifi_task");
	
	int nerrors = arg_parse(argc, argv, (void **) &sta_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, sta_args.end, argv[0]);
        return 1;
    }
    
   /* if (http_task_handle == NULL) {
	    send_wifi = true;
	    xTaskCreate(&http_task, "http_task", 8192, NULL, 5, &http_task_handle);
	    
	} else {
	    ESP_LOGI(TAG, "http_task already running");
	}*/
	
	if (mqtt_task_handle == NULL) {
	    send_wifi = true;
	    xTaskCreate(&mqtt_task, "mqtt_task", 8192, NULL, 5, &mqtt_task_handle);
	    
	} else {
	    ESP_LOGI(TAG, "http_task already running");
	}
	
	 
   
	return 0;
}

int stop_send_wifi_task (int argc, char **argv){
	ESP_LOGI(TAG, "Stopping WiFi task");
	send_wifi = false;
	return 0;
}

int is_wifi_connected(int argc, char **argv) {
   return (int)check_wifi_conn();
}

void register_wifi(void)
{
    sta_args.ssid = arg_str0(NULL, NULL, "<ssid>", "SSID of AP");
    sta_args.password = arg_str0(NULL, NULL, "<pass>", "password of AP");
    sta_args.end = arg_end(2);

    const esp_console_cmd_t sta_cmd = {
        .command = "sta",
        .help = "WiFi is station mode, join specified soft-AP",
        .hint = NULL,
        .func = &wifi_cmd_sta,
        .argtable = &sta_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&sta_cmd) );

    // my code starts
    wifi_send_args.value = arg_str0(NULL, NULL, "<val>", "value to send");
    wifi_send_args.end = arg_end(2);
    const esp_console_cmd_t wifi_cmd = {
        .command = "send_wifi",
        .help = "Start send data to Thingsboard by wifi",
        .hint = NULL,
        .func = &start_send_wifi_task,
        .argtable = &wifi_send_args 
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&wifi_cmd));
     
    const esp_console_cmd_t stop_wifi_cmd = {
        .command = "stop_send_wifi",
        .help = "Stop send data to Thingsboard by wifi",
        .hint = NULL,
        .func = &stop_send_wifi_task
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&stop_wifi_cmd));

    const esp_console_cmd_t is_wifi_connected_cmd = {
        .command = "check_wifi",
        .help = "is wifi connected",
        .hint = NULL,
        .func = &is_wifi_connected
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&is_wifi_connected_cmd));
    // my code ends


    scan_args.ssid = arg_str0(NULL, NULL, "<ssid>", "SSID of AP want to be scanned");
    scan_args.end = arg_end(1);

    const esp_console_cmd_t scan_cmd = {
        .command = "scan",
        .help = "WiFi is station mode, start scan ap",
        .hint = NULL,
        .func = &wifi_cmd_scan,
        .argtable = &scan_args
    };

    ap_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    ap_args.password = arg_str0(NULL, NULL, "<pass>", "password of AP");
    ap_args.end = arg_end(2);


    ESP_ERROR_CHECK( esp_console_cmd_register(&scan_cmd) );

    const esp_console_cmd_t ap_cmd = {
        .command = "ap",
        .help = "AP mode, configure ssid and password",
        .hint = NULL,
        .func = &wifi_cmd_ap,
        .argtable = &ap_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&ap_cmd) );

    const esp_console_cmd_t query_cmd = {
        .command = "query",
        .help = "query WiFi info",
        .hint = NULL,
        .func = &wifi_cmd_query,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&query_cmd) );

    const esp_console_cmd_t restart_cmd = {
        .command = "restart",
        .help = "Restart the program",
        .hint = NULL,
        .func = &restart,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&restart_cmd) );

    iperf_args.ip = arg_str0("c", "client", "<ip>", "run in client mode, connecting to <host>");
    iperf_args.server = arg_lit0("s", "server", "run in server mode");
    iperf_args.udp = arg_lit0("u", "udp", "use UDP rather than TCP");
    iperf_args.version = arg_lit0("V", "ipv6_domain", "use IPV6 address rather than IPV4");
    iperf_args.port = arg_int0("p", "port", "<port>", "server port to listen on/connect to");
    iperf_args.length = arg_int0("l", "len", "<length>", "set read/write buffer size");
    iperf_args.interval = arg_int0("i", "interval", "<interval>", "seconds between periodic bandwidth reports");
    iperf_args.time = arg_int0("t", "time", "<time>", "time in seconds to transmit for (default 10 secs)");
    iperf_args.abort = arg_lit0("a", "abort", "abort running iperf");
    iperf_args.end = arg_end(1);
    const esp_console_cmd_t iperf_cmd = {
        .command = "iperf",
        .help = "iperf command",
        .hint = NULL,
        .func = &wifi_cmd_iperf,
        .argtable = &iperf_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&iperf_cmd) );

    const esp_console_cmd_t heap_cmd = {
        .command = "heap",
        .help = "get min free heap size during test",
        .hint = NULL,
        .func = &heap_size,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&heap_cmd) );
}
