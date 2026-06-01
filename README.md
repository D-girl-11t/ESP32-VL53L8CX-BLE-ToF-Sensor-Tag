# ESP32 BLE Time-of-Flight Sensor Tag

This project is a battery-powered BLE sensor tag built around an ESP32 and a VL53L8CX Time-of-Flight (ToF) sensor. The system continuously captures distance measurements at a fixed rate, stores recent readings in memory, and streams data over Bluetooth Low Energy whenever a client connects.

The goal of this project was to build a complete embedded system, covering hardware design, power management, real-time firmware, BLE communication, and data buffering.

---

## What It Does

* Measures distance using the VL53L8CX ToF sensor
* Samples data at a fixed rate (20 Hz by default)
* Stores the most recent measurements in a ring buffer
* Streams live or buffered data over BLE
* Continues sampling even when BLE is disconnected
* Supports battery-powered operation with USB charging
* Places the sensor into a low-power state when sampling is disabled

---

## Hardware Overview

The hardware is based on an ESP32-C3 module connected to a VL53L8CX ToF sensor over I²C.

Power comes from either:

* A single-cell LiPo battery
* USB power during charging

A BQ-series charger with power-path management handles charging and automatically switches between USB and battery operation.

For debugging and bring-up, the board includes:

* Programming header
* Test points for VBAT, VSYS, 3V3, and GND
* Charger status monitoring points
* Clearly labeled silkscreen

### Sensor Interface

The VL53L8CX is connected over I²C and uses two additional GPIO signals:

* **INT** – Sensor interrupt output
* **LPn** – Sensor enable/low-power control

The LPn pin is used to enable or disable the sensor from firmware:

* LPn HIGH → Sensor active
* LPn LOW → Sensor in low-power mode

---

## Firmware Overview

To keep sensor acquisition independent from communication, the application is split into two tasks:

### Sensor Task

Responsible for:

* Reading data from the VL53L8CX
* Maintaining a fixed sampling rate
* Writing measurements into the ring buffer
* Tracking buffer overflows

### BLE Task

Responsible for:

* BLE advertising and connections
* Sending notifications
* Handling control commands
* Streaming buffered data

Since both tasks access the same buffer, a FreeRTOS mutex is used to protect shared data.

---

## Sampling Strategy

The default sampling rate is 20 Hz, which corresponds to one measurement every 50 ms.

The sampling task runs on a fixed schedule and is intentionally kept independent of BLE activity. This ensures that sensor timing remains consistent even when notifications are being transmitted.

One practical optimization is minimizing debug print statements. Excessive UART logging can introduce delays that affect timing accuracy, especially when working with millisecond-level scheduling.

If a sensor read ever takes longer than the allocated sampling window, that sample is dropped rather than delaying future measurements. Under normal operating conditions, this is not expected to occur with the selected VL53L8CX configuration.

---

## Ring Buffer Design

The assignment required storing the last **N seconds** of sensor data.

By default:

* Sampling rate = 20 Hz
* N = 10 seconds

This results in:

* 20 samples per second
* 200 samples stored in memory

For an 8×8 VL53L8CX ranging frame:

* 64 distance values per sample
* 4 bytes per value
* 256 bytes per frame

Memory usage is approximately:

* 5 KB per second of buffered data
* 50 KB for a 10-second history

The values of **N** and buffer size are currently fixed constants for simplicity but can easily be made configurable.

### Buffer Overflow Behavior

When the buffer becomes full, the oldest data is overwritten.

For this application, retaining the most recent measurements is more useful than preserving older data, so overwrite-on-full was chosen intentionally.

Different applications may require different strategies depending on how critical the data is.

---

## BLE Control Interface

The BLE service exposes two characteristics:

### Data Characteristic

Used for notifications.

When connected, the client receives:

* Live sensor data
* Previously buffered data

### Control Characteristic

Used for configuration and runtime control.

Supported commands:

* `startSampling`
* `stopSampling`
* `startBLE`
* `stopBLE`
* `SetRate:<value>`
* `Clear`

---

## Why Separate Sampling and BLE?

The original assignment mentioned Start and Stop commands, but it was unclear whether those commands should affect the sensor, BLE communication, or both.

To make the behavior explicit, the implementation separates them into two independent controls.

### Sampling Control

* `startSampling`
* `stopSampling`

These commands control whether the sensor is actively acquiring measurements.

### BLE Control

* `startBLE`
* `stopBLE`

These commands control whether data is transmitted to connected BLE clients.

This approach makes system behavior easier to understand and provides more flexibility during testing.

---

## Operating Modes

### Sampling Enabled, BLE Disabled

The sensor continues collecting measurements and storing them in the ring buffer.

If the buffer fills up, the oldest entries are overwritten.

### Sampling Disabled

The sensor is disabled through the LPn pin and enters a low-power state.

### Sampling Enabled, BLE Enabled

The system continues collecting measurements while simultaneously transmitting data over BLE.

When BLE is enabled, buffered measurements are sent first before transitioning to live streaming.

### BLE Disabled During Operation

Sensor acquisition continues normally, but measurements are only stored locally.

### Clear Command

Removes all buffered measurements and resets buffer tracking information.

---

## Power Management

The VL53L8CX supports a low-power mode through the LPn pin, which is used whenever sampling is stopped.

ESP32 light sleep and deep sleep were also evaluated. However, entering sleep causes BLE connections to disconnect, so sleep modes are only practical when both sensor acquisition and BLE communication are inactive.

Because of this, the system remains awake whenever BLE functionality is required.

---

## Testing

BLE functionality was tested using nRF Connect.

The following scenarios were verified:

* Sampling with BLE disabled
* Sampling with BLE enabled
* Stopping and restarting sensor acquisition
* Stopping and restarting BLE transmission
* Buffer overflow handling
* Clearing buffered data
* Streaming buffered measurements after reconnecting

The observed behavior matched the intended design in all test cases.

---

## References

VL53L8CX Driver Library:

[https://github.com/stm32duino/VL53L8CX](https://github.com/stm32duino/VL53L8CX)

