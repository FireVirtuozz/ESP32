#include "peripherals/pcnt_encoder.h"
#include "log_lib.h"

static const char *TAG = "pcnt_encoder_peripheral";

// --- Single channel ---

esp_err_t pcnt_single_channel_init(pcnt_single_channel_t *counter) {
    if (counter == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err;

    pcnt_unit_config_t unit_config = {
        .high_limit = counter->high_limit,
        .low_limit = -1, // never counts backwards
    };
    err = pcnt_new_unit(&unit_config, &counter->unit);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating PCNT unit on GPIO %d", esp_err_to_name(err), counter->pin);
        return err;
    }

    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = counter->glitch_filter_ns,
    };
    err = pcnt_unit_set_glitch_filter(counter->unit, &filter_config);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting glitch filter", esp_err_to_name(err));
        return err;
    }

    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = counter->pin,
        .level_gpio_num = -1, // no direction pin available
    };
    pcnt_channel_handle_t chan = NULL;
    err = pcnt_new_channel(counter->unit, &chan_config, &chan);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating PCNT channel", esp_err_to_name(err));
        return err;
    }

    // Rising edge: hold. Falling edge: increase (equivalent to a NEGEDGE count).
    err = pcnt_channel_set_edge_action(chan,
        PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting PCNT edge action", esp_err_to_name(err));
        return err;
    }

    err = pcnt_unit_enable(counter->unit);
    if (err != ESP_OK) return err;

    err = pcnt_unit_start(counter->unit);
    if (err != ESP_OK) return err;

    log_msg(TAG, "Single-channel PCNT counter initialized on GPIO %d", counter->pin);
    return ESP_OK;
}

esp_err_t pcnt_single_channel_drain(pcnt_single_channel_t *counter, int32_t *count) {
    if (counter == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = pcnt_unit_get_count(counter->unit, (int*)count);
    if (err != ESP_OK) {
        return err;
    }
    return pcnt_unit_clear_count(counter->unit);
}

uint32_t pcnt_sliding_window_push(pcnt_sliding_window_t *window, uint16_t new_sample) {
    if (window == NULL || window->samples == NULL || window->size == 0) {
        return 0;
    }

    window->samples[window->index] = new_sample;
    window->index = (window->index + 1) % window->size;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < window->size; i++) {
        sum += window->samples[i];
    }
    return sum;
}

// --- Quadrature ---

esp_err_t pcnt_quadrature_init(pcnt_quadrature_t *encoder) {
    if (encoder == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err;

    pcnt_unit_config_t unit_config = {
        .high_limit = encoder->high_limit,
        .low_limit = encoder->low_limit,
    };
    err = pcnt_new_unit(&unit_config, &encoder->unit);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating quadrature PCNT unit", esp_err_to_name(err));
        return err;
    }

    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = encoder->glitch_filter_ns,
    };
    err = pcnt_unit_set_glitch_filter(encoder->unit, &filter_config);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) setting glitch filter", esp_err_to_name(err));
        return err;
    }

    // Standard quadrature decoding: each channel counts on both edges of its
    // own signal, direction decided by the instantaneous level of the OTHER
    // channel. Two channels together give 4x resolution per detent.
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = encoder->pin_a,
        .level_gpio_num = encoder->pin_b,
    };
    pcnt_channel_handle_t chan_a = NULL;
    err = pcnt_new_channel(encoder->unit, &chan_a_config, &chan_a);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating PCNT channel A", esp_err_to_name(err));
        return err;
    }

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = encoder->pin_b,
        .level_gpio_num = encoder->pin_a,
    };
    pcnt_channel_handle_t chan_b = NULL;
    err = pcnt_new_channel(encoder->unit, &chan_b_config, &chan_b);
    if (err != ESP_OK) {
        log_msg(TAG, "Error (%s) creating PCNT channel B", esp_err_to_name(err));
        return err;
    }

    err = pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (err != ESP_OK) return err;
    err = pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (err != ESP_OK) return err;

    err = pcnt_channel_set_edge_action(chan_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    if (err != ESP_OK) return err;
    err = pcnt_channel_set_level_action(chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (err != ESP_OK) return err;

    err = pcnt_unit_enable(encoder->unit);
    if (err != ESP_OK) return err;

    err = pcnt_unit_start(encoder->unit);
    if (err != ESP_OK) return err;

    log_msg(TAG, "Quadrature PCNT encoder initialized on GPIO %d/%d", encoder->pin_a, encoder->pin_b);
    return ESP_OK;
}

esp_err_t pcnt_quadrature_drain(pcnt_quadrature_t *encoder, int32_t *delta) {
    if (encoder == NULL || delta == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = pcnt_unit_get_count(encoder->unit, (int*)delta);
    if (err != ESP_OK) {
        return err;
    }
    return pcnt_unit_clear_count(encoder->unit);
}
