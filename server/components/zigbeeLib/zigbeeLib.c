#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_coexist.h"

#if CONFIG_IDF_TARGET_ESP32C6

#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "logLib.h"

static const char *TAG = "zigbee_library";



static bool esp_zigbee_app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);
    const void *params = ezb_app_signal_get_params(app_signal);

    log_msg(TAG, "=== [ZIGBEE SIGNAL] %s (0x%02x) ===", 
             ezb_app_signal_to_string(signal_type), signal_type);

    switch (signal_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        log_msg(TAG, "Zigbee stack startup");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;

    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)params);
        if (status == EZB_BDB_STATUS_SUCCESS) {
            log_msg(TAG, "Device boot. Factory-reset: %s", ezb_bdb_is_factory_new() ? "Yes" : "No");
            if (ezb_bdb_is_factory_new()) {
                log_msg(TAG, "Launch network design");
                esp_err_t err = ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_FORMATION);
                if (err != ESP_OK) {
                    log_msg_lvl(ESP_LOG_ERROR, TAG, "Fail NETWORK_FORMATION : %s", esp_err_to_name(err));
                }
            } else {
                log_msg(TAG, "Existing network found. Automatic open..");
                ezb_bdb_open_network(180);
            }
        } else {
            log_msg_lvl(ESP_LOG_ERROR, TAG, "BDB launch fail (status: 0x%02x)", status);
        }
        break;
    }

    case EZB_BDB_SIGNAL_FORMATION: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)params);
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ezb_extpanid_t extended_pan_id;
            ezb_nwk_get_extended_panid(&extended_pan_id);
            log_msg(TAG, "Network succesfully formed. PAN ID: 0x%04hx, Channel: %d",
                     ezb_nwk_get_panid(), ezb_nwk_get_current_channel());
            
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        } else {
            log_msg_lvl(ESP_LOG_WARN, TAG, "Fail to form network (status: 0x%02x)", status);
        }
        break;
    }

    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)params);
        if (status == EZB_BDB_STATUS_SUCCESS) {
            log_msg(TAG, "Network steering completed. Network open to associations");
        } else {
            log_msg_lvl(ESP_LOG_WARN, TAG, "Sttering failure (status: 0x%02x)", status);
        }
        break;
    }

    case EZB_ZDO_SIGNAL_DEVICE_ANNCE: {
        const ezb_zdo_signal_device_annce_params_t *dev_annce_params = (const ezb_zdo_signal_device_annce_params_t *)params;
        log_msg(TAG, "New device linked. Short addr: 0x%04hx", dev_annce_params->short_addr);
        break;
    }

    case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
        uint8_t duration = *(uint8_t *)params;
        if (duration) {
            log_msg(TAG, "Network open for %d seconds.", duration);
        } else {
            log_msg_lvl(ESP_LOG_WARN, TAG, "Network closed");
        }
        break;
    }

    default:
        log_msg(TAG, "Zigbee signal recv : %s (0x%02x)", ezb_app_signal_to_string(signal_type), signal_type);
        break;
    }
    return true;
}

static void esp_zigbee_zcl_core_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
switch (callback_id) {
    case EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID: {
        ezb_zcl_set_attr_value_message_t *msg = (ezb_zcl_set_attr_value_message_t *)message;
        log_msg(TAG, "--- ZCL ATTR SET ---");
        log_msg(TAG, "From Endpoint: %d | Cluster ID: 0x%04x | Attr ID: 0x%04x", 
                 msg->info.dst_ep, msg->info.cluster_id, msg->in.attribute.id);
        break;
    }

    case EZB_ZCL_CORE_DEFAULT_RSP_CB_ID: {
        ezb_zcl_cmd_default_rsp_message_t *default_rsp = (ezb_zcl_cmd_default_rsp_message_t *)message;
        log_msg(TAG, "Default ZCL response recv. Status: 0x%02x", default_rsp->in.status_code);
        break;
    }

case EZB_ZCL_CORE_REPORT_ATTR_CB_ID: {
        ezb_zcl_cmd_report_attr_message_t *report = (ezb_zcl_cmd_report_attr_message_t *)message;
        
        log_msg(TAG, "--- ATTR REPORT RECV (ID: 5) ---");
        log_msg(TAG, "Dest Endpoint: %d | Cluster ID: 0x%04x | Status: 0x%02x", 
                 report->info.dst_ep, 
                 report->info.cluster_id,
                 report->info.status);

        ezb_zcl_report_attr_variable_t *var = report->in.variables;
        int i = 0;
        while (var != NULL) {
            log_msg(TAG, "  [%d] Attr ID: 0x%04x | Type: 0x%02x", i, var->attr_id, var->attr_type);
            
            if (var->attr_value != NULL) {
                switch (var->attr_type) {
                    case 0x10: // Boolean
                        log_msg(TAG, "  [%d] Value (Bool): %s", i, *(bool *)var->attr_value ? "TRUE" : "FALSE");
                        break;
                    case 0x20: // uint8_t
                        log_msg(TAG, "  [%d] Value (uint8): %u", i, *(uint8_t *)var->attr_value);
                        break;
                    case 0x21: // uint16_t
                        log_msg(TAG, "  [%d] Value (uint16): %u", i, *(uint16_t *)var->attr_value);
                        break;
                    case 0x01: // Type unknown
                        log_msg(TAG, "  [%d] Value (Type 0x01): 0x%02x (Raw)", i, *(uint8_t *)var->attr_value);
                        break;
                    case 0x23: // uint32_t
                        log_msg(TAG, "  [%d] Value (uint32): %lu", i, *(uint32_t *)var->attr_value);
                        break;
                    case 0x25: // uint48_t (48 bits stored on uint64_t securely)
                        {
                            uint64_t val = 0;
                            memcpy(&val, var->attr_value, 6);
                            log_msg(TAG, "  [%d] Value (uint48): %llu", i, val);
                        }
                        break;
                    default:
                        log_msg(TAG, "  [%d] Value (raw): 0x%02x (first byte)", i, *(uint8_t *)var->attr_value);
                        break;
                }
            } else {
                log_msg_lvl(ESP_LOG_WARN, TAG, "  [%d] Value : NULL", i);
            }
            
            var = var->next;
            i++;
        }
        
        report->out.result = EZB_ZCL_STATUS_SUCCESS;
        break;
    }

    default:
        log_msg_lvl(ESP_LOG_WARN, TAG, "Unhandled ZCL : ID (0x%04lx)", callback_id);
        break;
    }
}

static ezb_err_t esp_zigbee_create_coordinator_device(void)
{
    ezb_err_t err;

    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    ezb_zha_custom_gateway_config_t gateway_cfg = EZB_ZHA_CUSTOM_GATEWAY_CONFIG();
    
    // Endpoint 1 used for our gateway
    ezb_af_ep_desc_t ep_desc = ezb_zha_create_custom_gateway(1, &gateway_cfg);

    err = ezb_af_device_add_endpoint_desc(dev_desc, ep_desc);
    if (err != EZB_ERR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%u) adding ESP device endpoint", ezb_get_err_value(err));
        return err;
    }
    err = ezb_af_device_desc_register(dev_desc);
    if (err != EZB_ERR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) register ESP device", ezb_get_err_value(err));
        return err;
    }

    ezb_zcl_core_action_handler_register(esp_zigbee_zcl_core_action_handler);
    return EZB_ERR_NONE;
}

static void esp_zigbee_stack_main_task(void *pvParameters)
{

    esp_err_t err;
    ezb_err_t err_zb;

    esp_zigbee_config_t zigbee_config = {
        .device_config = {
            .device_type = EZB_NWK_DEVICE_TYPE_COORDINATOR,
        },
        .platform_config = {
            .storage_partition_name = "zb_storage",
            .radio_config = {
                .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,
            },
        },
    };

    err = esp_zigbee_init(&zigbee_config);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) init config zigbee", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // 0x07fff800 = Channels 11 to 26
    err_zb = ezb_bdb_set_primary_channel_set(0x07fff800);
    if (err_zb != EZB_ERR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) setting primary channel", ezb_get_err_value(err_zb));
        vTaskDelete(NULL);
        return;
    }
    err_zb = ezb_bdb_set_secondary_channel_set(0);
    if (err_zb != EZB_ERR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) setting secondary channel", ezb_get_err_value(err_zb));
        vTaskDelete(NULL);
        return;
    }

    ezb_aps_secur_enable_distributed_security(false);

    // Callbacks signals
    err_zb = ezb_app_signal_add_handler(esp_zigbee_app_signal_handler);
    if (err_zb != EZB_ERR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) adding signal callback", ezb_get_err_value(err_zb));
        vTaskDelete(NULL);
        return;
    }

    err_zb = esp_zigbee_create_coordinator_device();
    if (err_zb != EZB_ERR_NONE) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) creating coordinator device", ezb_get_err_value(err_zb));
        vTaskDelete(NULL);
        return;
    }

    err = esp_zigbee_start(false);
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) starting zigbee stack", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    err = esp_zigbee_launch_mainloop();
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error (%s) launching mainloop", esp_err_to_name(err));
    }

    vTaskDelete(NULL);
}

void send_cmd_on_off(uint16_t short_addr, uint8_t ep, bool on)
{
    ezb_zcl_on_off_cmd_t cmd = {0};

    // device info 
    cmd.cmd_ctrl.dst_addr.addr_mode = EZB_ADDR_MODE_SHORT;
    cmd.cmd_ctrl.dst_addr.u.short_addr = short_addr;
    cmd.cmd_ctrl.dst_ep = ep;
    cmd.cmd_ctrl.src_ep = 1; // Coordinator endpoint

    if (on) {
        ezb_zcl_on_off_on_cmd_req(&cmd);
        log_msg(TAG, "ON command sent to 0x%04x", short_addr);
    } else {
        ezb_zcl_on_off_off_cmd_req(&cmd);
        log_msg(TAG, "OFF command sent to 0x%04x", short_addr);
    }
}

void init_zigbee(void)
{

#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE && CONFIG_USE_WIFI
    esp_coex_wifi_i154_enable();
#endif


    esp_err_t err = nvs_flash_init_partition("zb_storage");
    if (err != ESP_OK) {
        log_msg_lvl(ESP_LOG_ERROR, TAG, "Error initialising zb_storage : %s. retry...", esp_err_to_name(err));
        nvs_flash_erase_partition("zb_storage");
        err = nvs_flash_init_partition("zb_storage");
    }

    xTaskCreate(esp_zigbee_stack_main_task, "zigbee_task", 4096 * 2, NULL, 5, NULL);
}

#endif