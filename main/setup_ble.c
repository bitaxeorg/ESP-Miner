#include "setup_ble.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_config.h"
#include "stratum_api.h"

#if defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED
#include "esp_bt.h"
#include "esp_mac.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#endif

static const char *TAG = "setup_ble";

#define SETUP_BLE_DEVICE_NAME_PREFIX "Bitaxe-"
#define SETUP_BLE_MAX_SSID_LEN 32
#define SETUP_BLE_MAX_WIFI_PASSWORD_LEN 64
#define SETUP_BLE_MAX_POOL_URL_LEN 80
#define SETUP_BLE_MAX_POOL_USER_LEN 80
#define SETUP_BLE_MAX_POOL_PASSWORD_LEN 64
#define SETUP_BLE_MAX_POOL_PORT_LEN 5
#define SETUP_BLE_MAX_COMMAND_LEN 16
#define SETUP_BLE_STATUS_LEN 64

typedef enum {
    SETUP_BLE_FIELD_WIFI_SSID = 0,
    SETUP_BLE_FIELD_WIFI_PASSWORD,
    SETUP_BLE_FIELD_POOL_URL,
    SETUP_BLE_FIELD_POOL_PORT,
    SETUP_BLE_FIELD_POOL_USER,
    SETUP_BLE_FIELD_POOL_PASSWORD,
    SETUP_BLE_FIELD_STATUS,
    SETUP_BLE_FIELD_COMMAND,
    SETUP_BLE_FIELD_COUNT,
} setup_ble_field_t;

typedef struct {
    char wifi_ssid[SETUP_BLE_MAX_SSID_LEN + 1];
    char wifi_password[SETUP_BLE_MAX_WIFI_PASSWORD_LEN + 1];
    char pool_url[SETUP_BLE_MAX_POOL_URL_LEN + 1];
    char pool_port[SETUP_BLE_MAX_POOL_PORT_LEN + 1];
    char pool_user[SETUP_BLE_MAX_POOL_USER_LEN + 1];
    char pool_password[SETUP_BLE_MAX_POOL_PASSWORD_LEN + 1];
    char command[SETUP_BLE_MAX_COMMAND_LEN + 1];
    char status[SETUP_BLE_STATUS_LEN];
    bool written[SETUP_BLE_FIELD_COUNT];
} setup_ble_state_t;

static setup_ble_state_t setup_ble_state;
static GlobalState *setup_ble_global_state;

#if defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED
static bool setup_ble_started;
static bool setup_ble_synced;
static bool classic_bt_mem_released;
static uint8_t setup_ble_own_addr_type;
static uint16_t setup_ble_status_val_handle;

static const ble_uuid128_t setup_ble_service_uuid = BLE_UUID128_INIT(
    0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f, 0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f);
static const ble_uuid128_t setup_ble_wifi_ssid_uuid = BLE_UUID128_INIT(
    0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);
static const ble_uuid128_t setup_ble_wifi_password_uuid = BLE_UUID128_INIT(
    0xa9, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);
static const ble_uuid128_t setup_ble_pool_url_uuid = BLE_UUID128_INIT(
    0xaa, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);
static const ble_uuid128_t setup_ble_pool_port_uuid = BLE_UUID128_INIT(
    0xab, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);
static const ble_uuid128_t setup_ble_pool_user_uuid = BLE_UUID128_INIT(
    0xac, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);
static const ble_uuid128_t setup_ble_pool_password_uuid = BLE_UUID128_INIT(
    0xad, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);
static const ble_uuid128_t setup_ble_status_uuid = BLE_UUID128_INIT(
    0xae, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);
static const ble_uuid128_t setup_ble_command_uuid = BLE_UUID128_INIT(
    0xaf, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);
#endif

static void setup_ble_copy_string(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) {
        return;
    }
    if (src == NULL) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static bool setup_ble_parse_port(const char *value, uint16_t *port)
{
    unsigned long parsed = 0;

    if (value == NULL || *value == '\0') {
        return false;
    }

    for (const char *p = value; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        parsed = parsed * 10 + (unsigned long)(*p - '0');
        if (parsed > UINT16_MAX) {
            return false;
        }
    }

    *port = (uint16_t)parsed;
    return true;
}

static void setup_ble_set_status(const char *status)
{
    setup_ble_copy_string(setup_ble_state.status, sizeof(setup_ble_state.status), status);

#if defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED
    if (setup_ble_started && setup_ble_status_val_handle != 0) {
        ble_gatts_chr_updated(setup_ble_status_val_handle);
    }
#endif
}

static void setup_ble_load_current_config(void)
{
    char *value = nvs_config_get_string(NVS_CONFIG_WIFI_SSID);
    setup_ble_copy_string(setup_ble_state.wifi_ssid, sizeof(setup_ble_state.wifi_ssid), value);
    free(value);

    setup_ble_state.wifi_password[0] = '\0';

    value = nvs_config_get_string(NVS_CONFIG_STRATUM_URL);
    setup_ble_copy_string(setup_ble_state.pool_url, sizeof(setup_ble_state.pool_url), value);
    free(value);

    snprintf(setup_ble_state.pool_port, sizeof(setup_ble_state.pool_port), "%u",
             nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT));

    value = nvs_config_get_string(NVS_CONFIG_STRATUM_USER);
    setup_ble_copy_string(setup_ble_state.pool_user, sizeof(setup_ble_state.pool_user), value);
    free(value);

    setup_ble_state.pool_password[0] = '\0';
    setup_ble_state.command[0] = '\0';
    memset(setup_ble_state.written, 0, sizeof(setup_ble_state.written));
    setup_ble_set_status("READY");
}

static void setup_ble_save_pool_url(const char *value)
{
    const char *tcp_prefix = "stratum+tcp://";
    const char *tls_prefix = "stratum+tls://";
    const char *ssl_prefix = "stratum+ssl://";
    const char *host = value;

    if (strncasecmp(host, tcp_prefix, strlen(tcp_prefix)) == 0) {
        host += strlen(tcp_prefix);
        nvs_config_set_u16(NVS_CONFIG_STRATUM_TLS, DISABLED);
    } else if (strncasecmp(host, tls_prefix, strlen(tls_prefix)) == 0) {
        host += strlen(tls_prefix);
        nvs_config_set_u16(NVS_CONFIG_STRATUM_TLS, BUNDLED_CRT);
    } else if (strncasecmp(host, ssl_prefix, strlen(ssl_prefix)) == 0) {
        host += strlen(ssl_prefix);
        nvs_config_set_u16(NVS_CONFIG_STRATUM_TLS, BUNDLED_CRT);
    }

    char normalized[SETUP_BLE_MAX_POOL_URL_LEN + 1];
    setup_ble_copy_string(normalized, sizeof(normalized), host);

    char *path = strchr(normalized, '/');
    if (path != NULL) {
        *path = '\0';
    }

    char *port_start = strrchr(normalized, ':');
    if (port_start != NULL) {
        uint16_t port;
        if (setup_ble_parse_port(port_start + 1, &port)) {
            *port_start = '\0';
            if (!setup_ble_state.written[SETUP_BLE_FIELD_POOL_PORT]) {
                nvs_config_set_u16(NVS_CONFIG_STRATUM_PORT, port);
            }
        }
    }

    nvs_config_set_string(NVS_CONFIG_STRATUM_URL, normalized);
}

static esp_err_t setup_ble_apply_config(void)
{
    uint16_t port = 0;

    if (setup_ble_state.written[SETUP_BLE_FIELD_POOL_PORT] &&
        !setup_ble_parse_port(setup_ble_state.pool_port, &port)) {
        setup_ble_set_status("ERROR_INVALID_POOL_PORT");
        return ESP_ERR_INVALID_ARG;
    }

    if (setup_ble_state.written[SETUP_BLE_FIELD_WIFI_SSID]) {
        nvs_config_set_string(NVS_CONFIG_WIFI_SSID, setup_ble_state.wifi_ssid);
    }
    if (setup_ble_state.written[SETUP_BLE_FIELD_WIFI_PASSWORD]) {
        nvs_config_set_string(NVS_CONFIG_WIFI_PASS, setup_ble_state.wifi_password);
    }
    if (setup_ble_state.written[SETUP_BLE_FIELD_POOL_URL]) {
        setup_ble_save_pool_url(setup_ble_state.pool_url);
    }
    if (setup_ble_state.written[SETUP_BLE_FIELD_POOL_PORT]) {
        nvs_config_set_u16(NVS_CONFIG_STRATUM_PORT, port);
    }
    if (setup_ble_state.written[SETUP_BLE_FIELD_POOL_USER]) {
        nvs_config_set_string(NVS_CONFIG_STRATUM_USER, setup_ble_state.pool_user);
    }
    if (setup_ble_state.written[SETUP_BLE_FIELD_POOL_PASSWORD]) {
        nvs_config_set_string(NVS_CONFIG_STRATUM_PASS, setup_ble_state.pool_password);
    }

    setup_ble_set_status("APPLIED_RESTART_REQUIRED");
    memset(setup_ble_state.written, 0, sizeof(setup_ble_state.written));
    return ESP_OK;
}

static void setup_ble_restart_task(void *param)
{
    (void)param;
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void setup_ble_handle_command(const char *command)
{
    if (strcmp(command, "APPLY") == 0) {
        setup_ble_apply_config();
    } else if (strcmp(command, "RESTART") == 0) {
        setup_ble_set_status("RESTARTING");
        xTaskCreate(setup_ble_restart_task, "ble_restart", 2048, NULL, 5, NULL);
    } else if (strcmp(command, "STATUS") == 0) {
        bool ap_enabled = setup_ble_global_state != NULL && setup_ble_global_state->SYSTEM_MODULE.ap_enabled;
        char status[SETUP_BLE_STATUS_LEN];
        snprintf(status, sizeof(status), "READY_AP_%d", ap_enabled ? 1 : 0);
        setup_ble_set_status(status);
    } else {
        setup_ble_set_status("ERROR_UNKNOWN_COMMAND");
    }
}

#if defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED
static void setup_ble_advertise(void);

static const char *setup_ble_get_field_value(setup_ble_field_t field)
{
    switch (field) {
        case SETUP_BLE_FIELD_WIFI_SSID:
            return setup_ble_state.wifi_ssid;
        case SETUP_BLE_FIELD_WIFI_PASSWORD:
            return setup_ble_state.wifi_password;
        case SETUP_BLE_FIELD_POOL_URL:
            return setup_ble_state.pool_url;
        case SETUP_BLE_FIELD_POOL_PORT:
            return setup_ble_state.pool_port;
        case SETUP_BLE_FIELD_POOL_USER:
            return setup_ble_state.pool_user;
        case SETUP_BLE_FIELD_POOL_PASSWORD:
            return setup_ble_state.pool_password;
        case SETUP_BLE_FIELD_STATUS:
            return setup_ble_state.status;
        case SETUP_BLE_FIELD_COMMAND:
            return setup_ble_state.command;
        default:
            return "";
    }
}

static int setup_ble_write_field(setup_ble_field_t field, const char *value, uint16_t len)
{
    char *dest = NULL;
    size_t max_len = 0;

    switch (field) {
        case SETUP_BLE_FIELD_WIFI_SSID:
            dest = setup_ble_state.wifi_ssid;
            max_len = SETUP_BLE_MAX_SSID_LEN;
            break;
        case SETUP_BLE_FIELD_WIFI_PASSWORD:
            dest = setup_ble_state.wifi_password;
            max_len = SETUP_BLE_MAX_WIFI_PASSWORD_LEN;
            break;
        case SETUP_BLE_FIELD_POOL_URL:
            dest = setup_ble_state.pool_url;
            max_len = SETUP_BLE_MAX_POOL_URL_LEN;
            break;
        case SETUP_BLE_FIELD_POOL_PORT:
            dest = setup_ble_state.pool_port;
            max_len = SETUP_BLE_MAX_POOL_PORT_LEN;
            break;
        case SETUP_BLE_FIELD_POOL_USER:
            dest = setup_ble_state.pool_user;
            max_len = SETUP_BLE_MAX_POOL_USER_LEN;
            break;
        case SETUP_BLE_FIELD_POOL_PASSWORD:
            dest = setup_ble_state.pool_password;
            max_len = SETUP_BLE_MAX_POOL_PASSWORD_LEN;
            break;
        case SETUP_BLE_FIELD_COMMAND:
            dest = setup_ble_state.command;
            max_len = SETUP_BLE_MAX_COMMAND_LEN;
            break;
        default:
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    if (len > max_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    memcpy(dest, value, len);
    dest[len] = '\0';
    setup_ble_state.written[field] = true;

    if (field == SETUP_BLE_FIELD_COMMAND) {
        setup_ble_handle_command(setup_ble_state.command);
    } else {
        setup_ble_set_status("PENDING_APPLY");
    }

    return 0;
}

static int setup_ble_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;

    setup_ble_field_t field = (setup_ble_field_t)(uintptr_t)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char *value = setup_ble_get_field_value(field);
        int rc = os_mbuf_append(ctxt->om, value, strlen(value));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        char value[SETUP_BLE_MAX_POOL_URL_LEN + 1];
        if (len >= sizeof(value)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        uint16_t copied = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, value, sizeof(value) - 1, &copied);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        value[copied] = '\0';
        return setup_ble_write_field(field, value, copied);
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int setup_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
        case BLE_GAP_EVENT_LINK_ESTAB:
            if (event->connect.status != 0 && setup_ble_started) {
                setup_ble_advertise();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            if (setup_ble_started) {
                setup_ble_advertise();
            }
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            if (setup_ble_started) {
                setup_ble_advertise();
            }
            return 0;
        default:
            return 0;
    }
}

static void setup_ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    int rc;

    /* Primary advertisement: flags + TX power + complete device name.
     * A legacy advertising PDU is limited to 31 bytes, so the bulky 128-bit
     * service UUID (18 bytes) goes in the scan response instead. */
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (const uint8_t *)ble_svc_gap_device_name();
    fields.name_len = strlen((const char *)fields.name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set BLE advertising data: %d", rc);
        return;
    }

    /* Scan response: complete list of 128-bit service UUIDs. */
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.uuids128 = &setup_ble_service_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set BLE scan response data: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(setup_ble_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, setup_ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start BLE advertising: %d", rc);
    }
}

static void setup_ble_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &setup_ble_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer BLE address type: %d", rc);
        return;
    }

    setup_ble_synced = true;
    setup_ble_advertise();
}

static void setup_ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static const struct ble_gatt_chr_def setup_ble_characteristics[] = {
    {
        .uuid = &setup_ble_wifi_ssid_uuid.u,
        .access_cb = setup_ble_access_cb,
        .arg = (void *)SETUP_BLE_FIELD_WIFI_SSID,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &setup_ble_wifi_password_uuid.u,
        .access_cb = setup_ble_access_cb,
        .arg = (void *)SETUP_BLE_FIELD_WIFI_PASSWORD,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &setup_ble_pool_url_uuid.u,
        .access_cb = setup_ble_access_cb,
        .arg = (void *)SETUP_BLE_FIELD_POOL_URL,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &setup_ble_pool_port_uuid.u,
        .access_cb = setup_ble_access_cb,
        .arg = (void *)SETUP_BLE_FIELD_POOL_PORT,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &setup_ble_pool_user_uuid.u,
        .access_cb = setup_ble_access_cb,
        .arg = (void *)SETUP_BLE_FIELD_POOL_USER,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &setup_ble_pool_password_uuid.u,
        .access_cb = setup_ble_access_cb,
        .arg = (void *)SETUP_BLE_FIELD_POOL_PASSWORD,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &setup_ble_status_uuid.u,
        .access_cb = setup_ble_access_cb,
        .arg = (void *)SETUP_BLE_FIELD_STATUS,
        .val_handle = &setup_ble_status_val_handle,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    {
        .uuid = &setup_ble_command_uuid.u,
        .access_cb = setup_ble_access_cb,
        .arg = (void *)SETUP_BLE_FIELD_COMMAND,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {0},
};

static const struct ble_gatt_svc_def setup_ble_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &setup_ble_service_uuid.u,
        .characteristics = setup_ble_characteristics,
    },
    {0},
};

static void setup_ble_build_device_name(char *name, size_t name_size)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(name, name_size, SETUP_BLE_DEVICE_NAME_PREFIX "%02X%02X", mac[4], mac[5]);
}
#endif

esp_err_t setup_ble_start(GlobalState *global_state)
{
    setup_ble_global_state = global_state;

#if defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED
    if (setup_ble_started) {
        return ESP_OK;
    }

    if (global_state == NULL || !global_state->SYSTEM_MODULE.ap_enabled) {
        ESP_LOGI(TAG, "Setup AP is disabled; BLE setup will not start");
        return ESP_OK;
    }

    setup_ble_load_current_config();

    if (!classic_bt_mem_released) {
        esp_err_t release_err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        if (release_err != ESP_OK && release_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to release Classic BT memory: %s", esp_err_to_name(release_err));
        }
        classic_bt_mem_released = true;
    }

    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE host: %d", rc);
        return ESP_FAIL;
    }

    char device_name[20];
    setup_ble_build_device_name(device_name, sizeof(device_name));
    ble_svc_gap_init();
    ble_svc_gatt_init();
    /* Must be set after ble_svc_gap_init(), which resets the name to the
     * compile-time default ("nimble"). */
    ble_svc_gap_device_name_set(device_name);

    rc = ble_gatts_count_cfg(setup_ble_services);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(setup_ble_services);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to register setup BLE GATT service: %d", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }

    ble_hs_cfg.sync_cb = setup_ble_on_sync;
    nimble_port_freertos_init(setup_ble_host_task);

    setup_ble_started = true;
    ESP_LOGI(TAG, "Setup BLE started as %s", device_name);
    return ESP_OK;
#else
    setup_ble_load_current_config();
    ESP_LOGW(TAG, "NimBLE is not enabled; setup BLE is unavailable");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void setup_ble_stop(void)
{
#if defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED
    if (!setup_ble_started) {
        return;
    }

    ESP_LOGI(TAG, "Stopping setup BLE");
    if (setup_ble_synced) {
        ble_gap_adv_stop();
    }

    int rc = nimble_port_stop();
    if (rc == 0) {
        nimble_port_deinit();
    } else {
        ESP_LOGW(TAG, "Failed to stop NimBLE host: %d", rc);
    }

    setup_ble_started = false;
    setup_ble_synced = false;
    setup_ble_status_val_handle = 0;
#endif
}
