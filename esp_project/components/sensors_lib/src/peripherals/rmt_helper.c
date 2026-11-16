#include "peripherals/rmt_helper.h"
#include "log_lib.h"

static const char *TAG = "rmt_helper_peripheral";

// --- RX ---

static bool IRAM_ATTR rmt_rx_done_callback(rmt_channel_handle_t channel,
    const rmt_rx_done_event_data_t *edata, void *user_data) {
    (void)channel;
    (void)edata;
    rmt_rx_helper_t *helper = (rmt_rx_helper_t *)user_data;
    BaseType_t task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(helper->done_sem, &task_awoken);
    return task_awoken == pdTRUE;
}

esp_err_t rmt_rx_helper_init(rmt_rx_helper_t *helper, int gpio_num,
    uint32_t resolution_hz, size_t mem_block_symbols) {
    if (helper == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    helper->done_sem = xSemaphoreCreateBinary();
    if (helper->done_sem == NULL) {
        log_msg(TAG, "Error creating RX semaphore for GPIO %d", gpio_num);
        return ESP_ERR_NO_MEM;
    }

    rmt_rx_channel_config_t rx_config = {
        .gpio_num = gpio_num,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = resolution_hz,
        .mem_block_symbols = mem_block_symbols,
    };

    esp_err_t err = rmt_new_rx_channel(&rx_config, &helper->channel);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating RMT RX channel on GPIO %d", esp_err_to_name(err), gpio_num);
        return err;
    }

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback,
    };
    err = rmt_rx_register_event_callbacks(helper->channel, &cbs, helper);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) registering RMT RX callbacks", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(helper->channel);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) enabling RMT RX channel", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "RMT RX channel initialized on GPIO %d", gpio_num);
    return ESP_OK;
}

esp_err_t rmt_rx_helper_capture(rmt_rx_helper_t *helper, rmt_symbol_word_t *buffer,
    size_t buffer_size, uint32_t signal_min_ns, uint32_t signal_max_ns, TickType_t wait_ticks) {
    if (helper == NULL || buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = signal_min_ns,
        .signal_range_max_ns = signal_max_ns,
    };

    esp_err_t err = rmt_receive(helper->channel, buffer, buffer_size, &receive_config);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) starting RMT capture", esp_err_to_name(err));
        return err;
    }

    return (xSemaphoreTake(helper->done_sem, wait_ticks) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

// --- TX ---

esp_err_t rmt_tx_helper_init(rmt_tx_helper_t *helper, int gpio_num,
    uint32_t resolution_hz, size_t mem_block_symbols,
    uint32_t carrier_freq_hz, float carrier_duty_cycle) {
    if (helper == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = gpio_num,
        .mem_block_symbols = mem_block_symbols,
        .resolution_hz = resolution_hz,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };

    esp_err_t err = rmt_new_tx_channel(&tx_config, &helper->channel);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating RMT TX channel on GPIO %d", esp_err_to_name(err), gpio_num);
        return err;
    }

    if (carrier_freq_hz > 0) {
        rmt_carrier_config_t carrier_config = {
            .frequency_hz = carrier_freq_hz,
            .duty_cycle = carrier_duty_cycle,
        };
        err = rmt_apply_carrier(helper->channel, &carrier_config);
        if (err != ESP_OK) {
            log_msg(TAG, "Error (%s) applying %" PRIu32 "Hz carrier", esp_err_to_name(err), carrier_freq_hz);
            return err;
        }
    }

    err = rmt_enable(helper->channel);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) enabling RMT TX channel", esp_err_to_name(err));
        return err;
    }

    rmt_copy_encoder_config_t copy_encoder_config = {};
    err = rmt_new_copy_encoder(&copy_encoder_config, &helper->encoder);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating RMT copy encoder", esp_err_to_name(err));
        return err;
    }

    log_msg(TAG, "RMT TX channel initialized on GPIO %d%s", gpio_num,
        carrier_freq_hz > 0 ? " (with carrier)" : "");
    return ESP_OK;
}

esp_err_t rmt_tx_helper_transmit(rmt_tx_helper_t *helper, const rmt_symbol_word_t *symbols, size_t symbol_count) {
    if (helper == NULL || symbols == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0, // send once
    };

    return rmt_transmit(helper->channel, helper->encoder,
        symbols, symbol_count * sizeof(rmt_symbol_word_t), &transmit_config);
}
