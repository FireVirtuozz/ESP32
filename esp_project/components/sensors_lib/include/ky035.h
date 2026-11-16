#ifndef KY035_H_
#define KY035_H_

#include <esp_err.h>

// KY-035: analog Hall effect sensor, used as a threshold-crossing speed
// sensor (magnet pass detection). Event-driven: a UDP frame is sent only
// when a new magnet pass is detected (not periodically), matching the
// original firmware. Payload: [signal_count: u64][signal_duration_us: i64].
#define KY035_ADC_UNIT ADC_UNIT_1
#define KY035_ADC_CHANNEL ADC_CHANNEL_6 // GPIO34 on ESP32
#define KY035_THRESHOLD_RAW 1800
#define KY035_POLL_PERIOD_MS 10

esp_err_t init_ky035(void);

#endif // KY035_H_
