# Ledlib library

This is a library to generate PWM signals. It has 8 timer channels to generate independant waveforms of PWM signals & 8 channels for high & low speed modes to control duties.

## Components used

- **ledc** for pwm control
- **FreeRTOS Task** for IRAM callback during fade
- **FreeRTOS** for mutex & thread-safe operations
- **gpio** for access to gpio pins

## Key concepts

### LEDC

**PWM signal** 
- Resolution (13 bits) -> duty from 0..8191
- Frequency (50hz = 20ms)
- Speed mode : High / Low
- Timer index : timer to associate a channel
- clk : source clock of timer
- hpoint : offset of rising edge

**Speed mode** 
- *High* : Allow glitch-free changeover of timer settings at next interrupt by hardware, suitable for real-time & high frequency pwm signals.
- *Low* : Triggered by software, for power saving controls.

**Timers**
There are 2 types of timers : *High* & *Low* Speed. LED_PWM allows 4 for high & 4 for low.

The *source clock* could be changed.

The resolution could be up to 20 bits depending on the frequency.
There is a hardware constraint regarding the frequency & resolution, to avoid a *duty cycle overflow* :  $f_{PWM} < \frac{f_{clock}}{2^{resolution}}$.
Example : For 20Khz, max resolution = 11 bits.


**Channel**
2 types of workflow :
- *Normal duty* : putting a new duty will change the signal directly
- *Fade duty* : it fades the signal, and emits an interrupt from hardware at the end in which a callback (in IRAM) can be registered.

### GPIO

**Pin state**
A pin can be *HIGH*, *LOW* or *floating*. The floating (Z-state) is important to be aware of. It is like a door, it could be closed & locked, open & lock, open and unlocked so a wind perturbation could switch the state. The pin is the same thing, and perturbations could be electromagnetic fields, switching noise, static electricity...

To fix this, a *pull up/down* is used to lock the pin at Low/High when in Z-state, and to counter perturbations.

When using a DC motor with H-Bridge, pull-down is more secure for PWM pins controlling the motor so that the motor is not activated by mistakes.

When reading values in input, we want reliable values, so a pull-up is more reliable in order not to read false values. It is required for communication protocols like I2C.

**Output modes**
The pin has two different output modes: Push-pull and Open-drain.

Push-pull (Standard): This is the most common mode. The pin uses two transistors to actively switch the signal between High (3.3V) and Low (GND). It provides a strong, fast signal, which is perfect for driving motors and servos.

Open-drain (Collective/Shared): Mostly used for communication protocols like I2C. The pin uses only one transistor to actively switch from High to Low.
When the state is Low, the pin is actively connected to Ground.
When the state is High, the pin becomes floating (High-Z).

This allows multiple devices to be connected to the same wire. Since no device ever "pushes" 3.3V (they only "pull" to Ground), that avoids a short-circuit if two devices try to talk at the same time. To get a High level, an external or internal pull-up resistor is required to "pull" the wire back to 3.3V when no one is pulling it down.

Concrete Example:

Use Push-pull for RC Steering Servo or Motor Controller (ESC). They need a strong, dedicated signal to react instantly without interference.

Use Open-drain for Gyroscope and OLED Screen if they share the same data lines (I2C). This allows them to "share" the wire without causing a short-circuit if they speak at the same time.

**IOMUX & GPIO Matrix**
The processor has internal *peripheral* functions (PWM, UART, SPI) and the chip has physical pins. The *IOMUX* acts as the primary switchboard. It decides which internal function is connected to which physical pin.

IOMUX vs GPIO Matrix:

*IOMUX*: High-speed, direct path but limited flexibility. Each pin has a set of fixed "preferred" functions.

*GPIO Matrix*: A fully flexible internal routing layer. It allows you to route almost any internal signal to almost any physical pin.

How they work together: By default, the IOMUX is set to its "GPIO Function". In this mode, the GPIO Matrix takes over to provide total flexibility (allowing you to reassign motors or servos via software without desoldering).

However, if the IOMUX is manually set to a dedicated function (like High-Speed SPI or UART), the GPIO Matrix is bypassed to ensure the fastest possible connection.

**Direction**
The pin direction could be set to output, input.

For communications like I2C, the pin needs to listen & send data so it needs to be input & output. By software (set_direction), it is very slow. The ESP provides an hardware solution allowing the pin to switch directions fast.

**Pin config**
- Current strength (mA)
- IOMUX / GPIO Matrix
- Pull up / down
- Open-Drain / Push-pull
- Direction (Input / Output)
- Sleeping status
- Inversed signal
- Output controlled by peripheral

**Interruptions**
Interruptions could be used for input GPIOs to register ISR functions when there are events like rising/falling edges of input signal.