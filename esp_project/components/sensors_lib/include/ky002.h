#ifndef KY002_H_
#define KY002_H_

#include <esp_err.h>

// !!! NAMING/SEMANTIC MISMATCH FOUND DURING REFACTOR, NOT FIXED HERE !!!
// The real KY-002 module is a single relay (digital OUTPUT, no sensing at
// all). The original implementation under CONFIG_USE_KY002 was instead a
// byte-for-byte copy of the FC-33 pulse-counting INPUT driver (same GPIO 23,
// same logic, and its log message even said "FC-33 initialized"). That
// strongly suggests this was a copy/paste mistake rather than an intentional
// design, but changing it risks breaking whatever the PC side currently
// expects from SENSOR_TYPE_KY002 telemetry. Behavior is preserved AS-IS
// below; please confirm on your side whether this should actually become a
// simple relay output (see actuators_lib for that pattern) or stay a pulse
// counter under a different, correctly-named sensor type.
#define KY002_GPIO 23

esp_err_t init_ky002(void);

#endif // KY002_H_
