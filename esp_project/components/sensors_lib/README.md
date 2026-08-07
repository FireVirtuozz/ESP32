# Sensors library

This is a library to control & understand several sensors.

## Components used

- **FreeRTOS** for tasks
- **gpio** for access to gpio pins, with ISR interruptions triggered by digital signals
- **pcnt** to count and filter digital signals
- **rmt** to send / receive digital signals based on timings
- **adc** to read analog signals
- **spi** to communicate with SPI protocol
- **i2c** to communicate with I2C protocol
- **udpLib** to send sensors data through UDP

## HC-SR04 : Ultrasonic sensor

This is an ultrasonic sensor to detect obstacles & estimate their distance in a range of 2cm - 4m.

**Principle** : The sensor emits a 40 kHz ultrasonic burst via its transmitter. The wave bounces off an obstacle and returns to the receiver. By measuring the time the wave takes to travel back (Time-of-Flight), the microcontroller can calculate the distance using the speed of sound ($340\text{ m/s}$). It can be up to 50ms.

**Pins**
- Trig: Input signal to trigger waves. Has to be a High signal for 10us.
- Echo: 5V high signal
- VCC / GND : works with 5V

## GY-91 : IMU

*Note: The hardware actually embeds a MPU6050 (6-axis IMU) and a BMP280.*

This module combines a 6-axis Inertial Measurement Unit (IMU) and a barometric sensor to track movement and environmental data.

**Principle**

- **Accelerometer (3-axis)**: Measures acceleration and gravity. It uses microscopic silicon springs that deflect under inertial forces, converting physical displacement into an electrical signal.
- **Gyroscope (3-axis)**: Measures angular velocity (rotation speed). It uses a vibrating structure that detects changes in orientation via the Coriolis effect.

- **BMP280 (Pressure & Temperature)**: 
  - *Pressure*: Uses a piezoresistive sensor on a silicon micro-membrane. Atmospheric pressure deflects the membrane, changing its electrical resistance.
  - *Temperature*: Uses a silicon diode element whose voltage drops linearly with temperature changes.

**Protocol**: I2C (both sensors share the same I2C bus but have different addresses). SPI is also possible.

**Pins**
- VIN : Power input (5V or 3.3V). The module features an onboard voltage regulator, allowing it to be safely powered by 5V
- 3.3V / GND : Works with 3.3V
- SCL / SDA : I2C Clock & Data
- SDD / SAO : Address select pin for the MPU6050. Disconnected/GND = 0x68 (default), High (3.3V) = 0x69
- NCS : Used to select SPI mode for the MPU6050
- CSB : Used to select SPI mode for the BMP280

## INA226 : Current & voltage monitor

Measuring current and voltage. Careful the current can be up to 1A.

**Principle**

The module uses a shunt resistance (0.1 Ohm) between IN- and IN+. By Ohm's law, we can determine the voltage, and we can observe voltage drops across of the shunt, that we can link to the exact current going through the circuit with an amplifier.

**Protocol**: I2C (0x40)

**Pins** 
- IN+ / IN- : out/in of shunt
- VBS : bus voltage input
- ALE (ALERT): Programmable interrupt pin (can alert the ESP32 if over-current or under-voltage occurs).
- SCL / SDA : I2C Clock & Data
- GND / VCC : Works with 3.3v

## KY-035 : Analog hall

This sensor measures the strength and direction (polarity) of a magnetic field. The signal can be obtained with an ADC.

**Principle**

It uses the Hall Effect. By using Lorentz force, it deviates electrons thus between 2 surfaces it creates an potential difference to be measured.

**Protocol**: OUT, can be measured with an ADC.

**Pins** 
- S : analog output signal
- GND / VCC : Works with either 3.3v or 5v. The output signal will be relative to it.

## RFID RC522 : RFID tag reader

This module reads RFID tags operating at 13.56 MHz

**Principle**

It works with electromagnetic induction. The RC522 antenna emits 13.56 MHz magnetic field. When an RFID tag enters the field, it creates a current that powers the chip (Refer to Maxwell-Faraday's equation). Then it modulates the magnetic field with a transistor to transmit its UID to the reader. The reader receives current variations and decodes it to a character using Manchester's principles.

**Protocol** : SPI

**Pins**

- VCC / GND : Works with 3.3v
- RST : reset pin (put to 0 for at least 10us then put it to 1 to hard reset and power on)
- MISO : Master In / Slave Out (RC522 -> ESP32)
- MOSI : Master Out / Slave In (ESP32 -> RC522)
- SCK : SPI Serial Clock
- SDA / NSS / CS : SPI Chip Select (0: active, 1: sleeping)

## RCWL 0515 : Doppler radar

This module is a motion detection radar working with doppler effect.

**Principle**

It emits microwaves at a frequency of 2.7GHz. It receives the bounce back, if there are no movements the frequency is not modified. If there are movements, the waves are compressed or stretched and the frequency changes. It can detecs through walls (not metal) and at a range of 12-15 meters. If there are no movements for ~3 seconds, the OUT pin falls to low.

**Protocol** : OUT

**Pins**

- VCC / GND : works with 4-18 voltage
- OUT: 1 if movement, 0 otherwise (3.3V)

## AS5600 : Magnetic rotary encoder

This module reads an angle using a magnet placed above the chip.

**Principle**

It uses multiple hall sensors to detect the orientation of a magnetic field.

**Protocol** : OUT (PWM) / I2C

**Pins**

- VCC / GND : works with 3.3V

- OUT: 12 bit PWM signal 

- SDA / SCL : I2C data & clock

- DIR : Direction for increasing angles (1=clockwise)

- GPO : One time programmable (OTP) memory, for zeroes

## KY 003 : Digital hall

Hall sensor as KY 035 but with digital OUT. 1 if there is a magnetic field, 0 else.

## FC 33 : Light blocking

This module detects if there is an object between its two prongs (U-shaped slot)

**Principle**

One side of the U contains an IR emitter, while the other side contains an IR receiver. When the receiver sees the IR light, OUT is 0. While there is an object, the receiver can't see the IR light so OUT is 1.

**Protocol**: OUT, use PCNT if frequent events

**Pins**

- VCC/GND: 3.3V

- OUT: 1 if there is an object, 0 otherwise

## KY 033 : Tracking

This module can detect the contrast of a surface beneath it, typically used to distinguish between light and dark surfaces (like a black line on a white floor).

**Principle**

The module carries an IR receiver / emitter. When the IR light encounters a black surface, it is absorbed and OUT goes to 0. While when it encounters an white surface, the light bounces back to the receiver and out goes to 1. It is possible to tune the sensitivity with a potentiometer

**Protocol**: OUT

- use PCNT if you want to filter noise and count pulses with accuracy
- (the ADC may be used to have a value related to the light intensity ??)

**Pins**

- VCC/GND: 3.3v

- OUT: signal

## KY-002 : Shock

This module can detect vibrations & impacts

**Principle**

The module carries a cylinder with a spring and a beam inside. The beam is not connected to the spring so the output is 1. When there is movement, the spring connects to the beam and the circuit is closed, so the output is 0.

**Protocol**: OUT

- You can use (ADC ?? or) PCNT to link the output to the vibration rate

**Pins**

- VCC/GND: 3.3v

- OUT: signal

## KY-040 : Rotary encoder

This module is a rotary encoder with a button at the middle.

**Principle**

A metallic slotted disc is connected to gnd. There are 2 pins. When the disk turns, it makes contact these 2 pins consequently. Is beam is reprensented by CLK, while the other one is DT. By checking which signal comes first, we can determine the rotational direction

**Protocol**: OUT

- PCNT if you want to counter pulses periodically
- GPIO if you want it to be event-triggered

**Pins**

- VCC/GND: 3.3v

- SW: Button

- CLK/DT: Clockwise (CLK before DT), Counter-clockwise otherwise

## DHT-11: Temp & Humidity

This module can read temperature and humidity.

**Principle**

- Temperature: Using a NTC thermistor (resistance decreases as temperature increases)

- Humidity: There are 2 electrodes with a sponge-like polymer above it. When the air is humid, the sponge catch water molecules, releasing ions, so the current can more easily pass through (resistance decreases). When the air is dry, the sponge does not produces ions and the resistance becomes huge.

Then the internal chip (8-bit MCU) link theses resistances to digital values with voltage drops. And it send it with a special protocol handled by timings on one pin OUT. When the pin is woken up (~18ms LOW), the sensor sends LOW for 80us then HIGH for 80us, then sends a 40 bits buffer. When the signal is HIGH for ~25us, it is a 0, for ~75us it is a 1. The buffer is this:

humidity int | humidity dec | temp int | temp dec | checksum

*Careful: Because of these timings, the sensor should not be triggered at a period below 1 second*

**Protocol** OUT - RMT TX & RX

- use RMT to control precisely the timings without using the CPU.

**Pins**

- VCC/GND: 3.3v

- OUT: 40 bits buffer

## KY-020 : Ball switch

This module is useful to monitor wether if an object falls over.

**Principle**

There is a ball in a confined cylinder with 2 pins at its extremities. Each time the ball touches it, it triggers a signal.

**Protocol** GPIO OUT

**Pins**
- VCC/GND: 3.3v
- OUT: digital signal

## KY-018 : Photoresistor

This module is useful to detect if there is any light in a room.

**Principle**

A photoresistor is designed with a semiconducting material. The electrons are stuck when there is no light, so there is no current going through, the resistance is huge. When there is light, photons free the electrons and the resistance decreases. The signal can be measured with an ADC, representing the amount of light.

**Protocol** OUT - ADC

**Pins**
- VCC/GND: 3.3v
- OUT: analog signal

## KY-031 : Tap module

This module is useful to trigger event by knocking this one.

**Principle**

There is a spring and a pin seperated when idle. When the module is hit, the spring connects to the pin and triggers the signal.

**Protocol** GPIO DIGITAL

**Pins**
- VCC/GND: 3.3v
- OUT: digital signal

## KY-017 : Tillt-swich

This module is the same thing a the *KY-020* but with a mercury ball, making it more reliable.

## KY-005 : IR emitter

This module can emits IR signals.

**Principle**

A LED emits a non-visible 940nm light. This LED emit a light at a frequency of 38 kHz, such as the receiver. The communication protocols between the receiver and the emitter are established with timings of activation.

For example, the NEC protocol.
- Header: 9.5ms + 4.5ms pause
- Data: 0 = 1.125ms, 1 = 2.25ms (565.5us signal + 1.6875ms pause)

That makes the RMT the perfect component to handle this.

**Protocol** RMT TX

**Pins**
-VCC/GND: 3.3v (or 5v, range ~1.5m -> ~8m, adapt resistance)
-OUT: signal (RMT TX) - *careful: 100 ohm resistance to protect the LED*

## KY-022 : IR receiver

This module can receive IR lights.

**Principle**

The black resin cylinder is designed to filter visible light, allowing only infrared light to pass through. A band-pass filter is here allowing only the light clicking at 38kHz. Then the demodulator is converting the burst signal into squared signals.

So, we can read the timings on the OUT pin with RMT. For example, by applying NEC's protocol timings, we can communicate with the *KY-005*.

**Protocol** RMT RX

**Pins**
- VCC/GND: 3.3v
- OUT: signal (RMT RX)

## KY-021 : Reed switch

This module can detect magnetic fields, such as hall sensors.

**Principle**

The are 2 magnetic beams that are facing each other. When they are put in a magnetic field, one becomes north pole and the other one becomes the south pole, making them stick to eath other so the current can pass through.

**Protocol** Digital GPIO

**Pins**
- VCC/GND: 3.3v
- OUT: digital signal

## KY-004: Button

This is a simple button

**Principle**

When the button is pressed, a metallic dome (spring-like shape) goes into an unstable state and connects to the circuit, allowing the current to pass through

**Protocol** Digital GPIO

**Pins**
- VCC/GND: 3.3v
- OUT: Digital signal

## KY-039: Heartbeat sensor

This module is useful to monitor our heartbeat rate.

**Principle**

There is an IR emitter such as the ky005, and an infrared photoresistor. When the blood is at ease, the infared light emitted is not passing as the same way that when a heartbeat comes. So, the photoresistor monitors it and converts its to a value that can be measured with an ADC.

**Protocol** ADC

**Pins**
- GND/VCC: 3.3v
- OUT: analog signal

## KY-032: Avoidance

This modules detects if there are obstacles in front of it using infrared light.

**Principle**

There is an IR emitter next to an IR receiver working both at 38 kHz. There is potentiometer that can modify the emitting frequency (35 - 42 kHz), and another one for the current delivered, allowing the module to detect further away obstacles.

**Protocol** Digital GPIO - gpio glitch filter?

**Pins**
- VCC/GND: 3.3v
- OUT: digital signal

## KY-023: Joystick

This module is a joystick.

**Principle**

There are 2 analog potentiometers for the X & Y axis. There is also a button (such as *KY-004*).

**Protocol** ADC for X,Y & GPIO digital for SW (button)

**Pins**
- VCC/GND: 3.3v
- SW: Button digital signal
- VRx: analog X-axis signal
- VRy: analog Y-axis signal
