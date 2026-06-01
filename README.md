# ESP32 BLE Time-of-Flight Sensor Tag

A battery-powered Bluetooth Low Energy (BLE) sensor tag built around an ESP32 and the VL53L8CX Time-of-Flight (ToF) sensor. The system performs fixed-rate distance measurements, stores recent data in a ring buffer, and streams measurements over BLE when a connection is available.

The project demonstrates embedded system design across hardware, firmware, power management, BLE communication, and real-time task scheduling using FreeRTOS.

---

## Features

### Hardware

* ESP32-C3 / ESP32-S3 based design
* VL53L8CX Time-of-Flight sensor connected over I²C
* Single-cell LiPo battery support
* USB charging with BQ-series charger and power-path management
* Dedicated 3.3 V power rail for MCU and sensor
* Programming/debug header
* Test points for board bring-up and validation
* Low-power sensor control using the VL53L8CX LPn pin

### Firmware

* FreeRTOS task architecture
* Fixed-rate sensor sampling independent of BLE activity
* Ring buffer storing the last N seconds of data
* BLE GATT service with custom Data and Control characteristics
* Buffered data transmission after reconnection
* Buffer overflow tracking
* Optional low-power operation when sampling is disabled

---

# System Architecture

```text
                USB Input
                    │
                    ▼
         ┌─────────────────────┐
         │ BQ Charger +        │
         │ Power Path Manager  │
         └─────────┬───────────┘
                   │
                 VSYS
                   │
                   ▼
         ┌─────────────────────┐
         │ 3.3V Regulation     │
         └─────────┬───────────┘
                   │
      ┌────────────┴────────────┐
      │                         │
      ▼                         ▼
 ┌─────────┐              ┌──────────┐
 │ ESP32   │───I²C───────►│ VL53L8CX │
 │ BLE MCU │              │ ToF      │
 └─────────┘              └──────────┘
      │                         │
      │                         │
      └──────INT / LPn──────────┘
```

---

# Hardware Design

## Power System

The device operates from either:

* USB power
* A single-cell LiPo battery

A BQ-series charger with integrated power-path management allows the system to remain operational while charging.

### Power Rails

| Rail | Description                             |
| ---- | --------------------------------------- |
| VBAT | LiPo battery voltage                    |
| VSYS | System supply from charger power path   |
| 3V3  | Regulated supply for ESP32 and VL53L8CX |

### Charger Configuration

The charger is configured using external resistor settings for:

* Charge current
* Charge termination current
* Battery safety limits

When USB power is present:

* The system is powered from USB through the power-path circuit.
* The battery charges simultaneously.

When USB is removed:

* Operation automatically switches to battery power.

---

## VL53L8CX Integration

The VL53L8CX communicates with the ESP32 using I²C.

### Signal Connections

| Sensor Pin | Function                          |
| ---------- | --------------------------------- |
| SDA        | I²C Data                          |
| SCL        | I²C Clock                         |
| INT        | Measurement Ready Interrupt       |
| LPn        | Sensor Enable / Low-Power Control |

### LPn Usage

The LPn pin is connected to a GPIO on the ESP32.

* LPn HIGH → Sensor active
* LPn LOW → Sensor disabled / low-power mode

When sampling is stopped, firmware drives LPn low to reduce sensor power consumption.

### I²C Design

* Pull-up resistors included on SDA and SCL
* Shared 3.3 V logic domain
* No level shifters required

---

## Bring-Up Features

To simplify testing and debugging, dedicated test points are provided for:

* VBAT
* VSYS
* 3V3
* GND
* Charger status signals

Additional features include:

* Programming header
* Debug access
* Silkscreen labeling for all major interfaces

---

# Firmware Architecture

Two primary tasks manage system operation.

## SensorTask

Responsible for:

* VL53L8CX configuration
* Fixed-rate sampling
* Ring-buffer storage
* Overflow tracking

The task operates independently of BLE activity.

### Sampling Rate

Default sampling rate:

```text
20 Hz
```

Sampling period:

```text
50 ms
```

One ranging frame is captured every 50 ms.

To preserve timing accuracy:

* Sampling runs on a fixed schedule.
* Excessive UART logging is avoided.
* Samples exceeding the timing budget are dropped rather than delaying future acquisitions.

---

## BLETask

Responsible for:

* BLE advertising
* Connection management
* Notification transmission
* Processing control commands

BLE operation never blocks sensor acquisition.

---

## Thread Safety

The ring buffer is accessed by both SensorTask and BLETask.

A FreeRTOS mutex protects shared data structures to prevent concurrent read/write corruption.

---

# Ring Buffer Design

The assignment requires storage of the last N seconds of sensor data.

Default configuration:

* Sampling rate = 20 Hz
* N = 10 seconds

Therefore:

```text
20 samples/sec × 10 sec = 200 samples
```

### Memory Usage

The VL53L8CX provides an 8×8 ranging grid.

Per sample:

```text
64 values × 4 bytes = 256 bytes
```

Per second:

```text
20 samples × 256 bytes = 5120 bytes
≈ 5 KB/sec
```

For N seconds:

```text
Buffer Size = 5120 × N bytes
```

For N = 10:

```text
≈ 50 KB
```

The buffer duration and capacity are currently compile-time constants but can be made configurable in future revisions.

---

## Buffer Overflow Handling

Distance measurements are considered transient data.

When the buffer becomes full:

* New samples overwrite the oldest samples.
* Overflow events are counted.

This ensures the system always retains the most recent measurements.

---

# BLE GATT Service

## Data Characteristic

Properties:

* Notify

Purpose:

* Live sensor streaming
* Buffered data transmission after reconnection

---

## Control Characteristic

Properties:

* Write

Supported commands:

| Command         | Description                |
| --------------- | -------------------------- |
| startSampling   | Enable sensor acquisition  |
| stopSampling    | Disable sensor acquisition |
| startBLE        | Enable BLE transmission    |
| stopBLE         | Disable BLE transmission   |
| SetRate:<value> | Change sampling rate       |
| Clear           | Clear buffered samples     |

---

# Design Assumptions

The original assignment specified generic Start and Stop commands.

To remove ambiguity, the implementation separates sensor acquisition from BLE transmission.

### Sensor Control

```text
startSampling
stopSampling
```

Controls whether the VL53L8CX is actively collecting measurements.

### BLE Control

```text
startBLE
stopBLE
```

Controls whether data is transmitted over BLE.

This separation provides greater flexibility and clearer system behavior.

---

# Operating Modes

## Sampling Enabled, BLE Disabled

```text
startSampling
stopBLE
```

Behavior:

* Sensor acquires data
* Measurements stored in ring buffer
* Oldest data overwritten when full

---

## Sampling Disabled

```text
stopSampling
```

Behavior:

* Sensor inactive
* LPn driven LOW
* No new measurements acquired

---

## Sampling Enabled, BLE Enabled

```text
startSampling
startBLE
```

Behavior:

* Sensor continues sampling
* Live data streamed over BLE
* Buffered measurements transmitted first
* Ring buffer gradually drains

---

## BLE Disabled During Operation

```text
stopBLE
```

Behavior:

* Notifications stop
* Sampling continues
* New data accumulates in ring buffer

---

## Clear Command

```text
Clear
```

Behavior:

* Removes all buffered samples
* Resets buffer indices
* Resets overflow counters

---

# Power Management

## Sensor Low-Power Mode

The VL53L8CX LPn pin is used for sensor power management.

When sampling is disabled:

* LPn is driven LOW
* Sensor enters low-power mode

---

## ESP32 Sleep Modes

Light sleep and deep sleep were evaluated.

A limitation is that BLE connections are lost when entering sleep.

For this reason:

* Sleep modes are only suitable when BLE transmission is not required.
* The ESP32 remains awake while BLE services are active.

---

# Testing

BLE functionality was validated using nRF Connect.

Tested scenarios:

* Sensor sampling with BLE disabled
* Sensor disabled
* Sensor sampling with BLE enabled
* BLE disabled during acquisition
* Buffer clearing
* Buffer overflow handling
* Buffered transmission after reconnection

Observed behavior matched the intended system design.

---

# References

VL53L8CX Driver and Example Code:

https://github.com/stm32duino/VL53L8CX


