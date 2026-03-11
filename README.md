| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- |

# Hall Sensor RPM Meter Example

## Overview

This example implements an RPM meter using a digital-output Hall-effect sensor.
It measures the time between rising edges on the sensor output, averages several
recent periods, and converts that to revolutions per minute (RPM). The current
RPM and related statistics are printed to the log whenever they change or at a
fixed interval.

Use this as a starting point for any project where you need to measure shaft
speed with a magnet and Hall sensor (for example, fans, wheels, or motors).

## Hardware Required

- **ESP32 development board**: Any board based on a supported target.
- **Digital Hall-effect sensor**: With open-collector or push-pull digital output.
- **Magnet**: Mounted on the rotating object.
- **Wiring / breadboard** as needed.

## Pin Assignment

By default, the example expects the Hall sensor digital output to be connected
to GPIO `4`:

| Signal                | ESP GPIO | Notes                         |
| --------------------- | -------- | ----------------------------- |
| Hall sensor output    | GPIO 4   | Configured as interrupt input |
| Hall sensor VCC       | 3V3/5V   | According to sensor specs     |
| Hall sensor GND       | GND      | Common ground with ESP        |

You can change the GPIO by editing `HALL_DIGITAL_GPIO` in `main/i2c_basic_example_main.c`
and rebuilding the project.

## Configuration

The behavior of the RPM meter can be tuned via the following defines in
`main/i2c_basic_example_main.c`:

- **`RPM_DISPLAY_INTERVAL_MS`**: Minimum time between log prints.
- **`RPM_MIN_PERIOD_MS` / `RPM_MAX_PERIOD_MS`**: Valid period range used to
  filter out noise and unrealistic measurements.
- **`RPM_AVERAGE_COUNT`**: Number of recent periods to average when computing RPM.

## Build and Flash

1. **Set up ESP-IDF** following the official Getting Started guide.
2. In the project directory, run:

   ```bash
   idf.py set-target esp32   # or your specific target
   idf.py -p PORT flash monitor
   ```

3. Replace `PORT` with the serial port of your board.

To exit the monitor, press `Ctrl+]`.

## Example Output

Typical log output (tag `rpm_meter`) might look like:

```bash
I (123) rpm_meter: GPIO interrupt initialized successfully (pin 4)
I (133) rpm_meter: Hall sensor initialized successfully
I (143) rpm_meter: RPM meter ready. Monitoring rotation...
I (2350) rpm_meter: RPM: 720.5 | Period: 83.3 ms | Samples: 5/5 | Total: 42
```

When there is no rotation, you will periodically see:

```bash
I (5000) rpm_meter: RPM: -- | Waiting for rotation...
```

## Notes

- Make sure the magnet passes close enough to the Hall sensor to generate a
  clean digital pulse.
- If the RPM readings are noisy, consider adjusting the minimum/maximum period
  or increasing the averaging count.

