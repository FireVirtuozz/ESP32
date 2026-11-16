#include "ky005.h"
#include "peripherals/rmt_helper.h"
#include "log_lib.h"

static const char *TAG = "ky005_actuator";

#if CONFIG_USE_KY005

#define KY005_RMT_RESOLUTION_HZ 1000000 // 1 tick = 1us
#define KY005_CARRIER_FREQ_HZ 38000
#define KY005_CARRIER_DUTY 0.33f

// NEC protocol timings, in microseconds.
#define NEC_HEADER_HIGH 9000
#define NEC_HEADER_LOW  4500
#define NEC_BIT_HIGH    560
#define NEC_ZERO_LOW    560
#define NEC_ONE_LOW     1690

static rmt_tx_helper_t tx;

esp_err_t init_ky005(void) {
    return rmt_tx_helper_init(&tx, KY005_GPIO, KY005_RMT_RESOLUTION_HZ, 64,
        KY005_CARRIER_FREQ_HZ, KY005_CARRIER_DUTY);
}

esp_err_t ky005_send_code(uint32_t code) {
    rmt_symbol_word_t symbols[2 + 32 + 1]; // header + 32 bits + trailing mark
    size_t idx = 0;

    symbols[idx++] = (rmt_symbol_word_t){
        .duration0 = NEC_HEADER_HIGH, .level0 = 1,
        .duration1 = NEC_HEADER_LOW, .level1 = 0,
    };

    for (int i = 31; i >= 0; i--) {
        bool bit = (code >> i) & 0x01;
        symbols[idx++] = (rmt_symbol_word_t){
            .duration0 = NEC_BIT_HIGH, .level0 = 1,
            .duration1 = bit ? NEC_ONE_LOW : NEC_ZERO_LOW, .level1 = 0,
        };
    }

    symbols[idx++] = (rmt_symbol_word_t){
        .duration0 = NEC_BIT_HIGH, .level0 = 1,
        .duration1 = 0, .level1 = 0,
    };

    esp_err_t err = rmt_tx_helper_transmit(&tx, symbols, idx);
    if (err == ESP_OK) {
        log_msg(TAG, "IR code sent: 0x%08" PRIX32, code);
    }
    return err;
}

#else // !CONFIG_USE_KY005

esp_err_t init_ky005(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ky005_send_code(uint32_t code) { (void)code; return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USE_KY005
