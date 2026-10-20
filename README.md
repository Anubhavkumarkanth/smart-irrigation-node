# Smart Irrigation Node

ESP32 firmware that waters a plant when the soil gets dry and reports to an MQTT broker. A capacitive soil-moisture sensor goes on an ADC pin, a relay switches the pump, and a float switch stops the pump if the water tank runs empty.

## How it works

- Reads the soil sensor (16 samples averaged, then lightly smoothed) and converts the voltage to a 0-100% moisture value using a two-point dry/wet calibration.
- Starts the pump when moisture drops below `start_below` (default 35%) and stops it at `stop_above` (default 55%). The gap between the two numbers stops the pump from switching on and off around a single threshold.
- A single run is capped at 20 s, and after every run the pump rests for 60 s so the water can soak in before the soil is checked again.
- The pump is switched off straight away if the float switch reports an empty tank or the sensor reading looks wrong (unplugged or shorted probe). It starts again by itself once the problem is gone.
- Watering keeps working when WiFi or the broker is down.

States: `idle`, `watering`, `resting`, `tank_empty`, `sensor_fault`.

## Wiring

| ESP32 pin | Connected to |
| --- | --- |
| GPIO34 | soil sensor analog output |
| GPIO26 | relay module input (pump) |
| GPIO27 | float switch, other side to GND (LOW = water present) |
| 3V3 / GND | sensor and relay module power |

GPIO34 is on ADC1, which keeps working while WiFi is on.

## MQTT

Topics start with `irrigation/<DEVICE_ID>/`.

| Topic | Direction | Payload |
| --- | --- | --- |
| `telemetry` | device to broker, every 10 s | JSON with moisture, soil_mv, state, pump, tank_ok, runs, thresholds, uptime_s |
| `status` | device to broker | `online` or `offline` (retained, last will) |
| `cmd/start_below` | broker to device | number, 0-100 |
| `cmd/stop_above` | broker to device | number, 0-100 |

A command that is not a valid number, or that would put `start_below` at or above `stop_above`, is ignored.

## Build and flash

Put your WiFi details and a unique `DEVICE_ID` at the top of `src/main.cpp`, then:

```
pip install platformio
pio run -t upload
```

`test.mosquitto.org` is a public broker, so anyone can see what you publish there.

## Tests

The decision logic lives in `lib/irrigation` and does not depend on Arduino, so it is tested on a PC:

```
make test
```

That builds with `-Werror` and the address and undefined-behaviour sanitizers and runs 52 checks: calibration, hysteresis, max run time, rest time, empty tank, sensor faults, `millis()` wrap-around and command parsing.

## Status

- Control logic: unit tested. Firmware: builds for `esp32dev` with no warnings.
- Not run on real hardware yet, so the calibration values (2800 mV dry, 1200 mV wet) are placeholders. Measure your own sensor in air and in water.
- A sensor that gets stuck reading "dry" would keep the pump cycling, limited only by the run and rest times.
- Driving a real pump needs a relay or MOSFET stage with a flyback diode, not a GPIO pin directly.
