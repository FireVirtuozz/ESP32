# hardware

KiCad project of the PCB of the RC car, with the power and wiring of the car.

<!-- TODO: add the schematic (PDF or PNG) and a render of the PCB here -->

## Files

<!-- TODO: list what is in this folder, ex: kicad/ (project), gerbers/, schematic.pdf, pcb.png -->

## Power

```
Battery -> switch -> ACS758 -> 2x Wago (14 AWG)

Wago 1: battery 7.3 V in, 2200 uF polymer cap, BTS7960 B+ (then M+/M- to the motor),
        voltage divider (10 k / 5.1 k + 100 nF) to the battery ADC, link to Wago 2
Wago 2: 7.3 V -> Mini560 5 V (-> PCB)
                 Mini560 3.3 V (-> PCB)
```

PCB power inputs have a 100 nF ceramic cap + a polymer cap in parallel (470 uF on 5 V, 220 uF on 3.3 V). Tracks are 1.5 mm wide on 5 V and 1 mm on 3.3 V.

## PCB connections

| Module | Supply | Signals |
|---|---|---|
| BNO085 | 3.3 V | I2C, INT (GPIO), RST (GPIO) |
| TOF400C | 3.3 V | I2C |
| ACS758 | 3.3 V | analog out -> 1 k + 100 nF filter -> ADC |
| FC-33 | 3.3 V | pulse -> PCNT |
| KY-006 | 3.3 V | LEDC |
| BTS7960 | 5 V | R_PWM / L_PWM (LEDC), R_EN and L_EN tied to 3.3 V |
| MG996R | 5 V | PWM (LEDC) |
| D500 lidar | 5 V | UART RX only, motor PWM pin tied to GND |
| BZ181 GNSS | 5 V | UART RX / TX |
| HC-SR04 | 5 V | trigger (GPIO), echo (GPIO, 1 k / 2 k divider to 3.3 V) |

The I2C bus has a footprint for 4.7 k pull-ups on SDA and SCL. The camera board is powered from the PCB through a 2.54 mm header (with a 100 nF + 10 uF filter).