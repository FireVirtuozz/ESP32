# Sensors library

This is a library to control & understand several sensors.

## TODO

- send raw values by udp, delete dead code. For BMP, send calibration values special frame at the beginning.
- KY: implement behaviour with ESP's ADC functions.

## HC-SR04

This is an ultrasonic sensor to detect obstacles & estimate their location in a range of 2cm - 4m.

Datasheet: cf url

**Principle** : One (thing?) emits ultrasonic waves, another receive the echo of theses waves. Approx time : 50us?

**Pins**
- *Trig*: Input signal to trigger waves. Has to be a High signal for 10us.
- *Echo*: 5V high signal
- VCC / GND : works with 5V
