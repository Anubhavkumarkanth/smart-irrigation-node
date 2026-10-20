#pragma once

#include <cstdint>

namespace irrigation {

// Capacitive soil sensors read a LOWER voltage when the soil is wetter.
// dry_mv is the reading with the probe in open air, wet_mv with it in water.
struct SoilCalibration {
    float dry_mv = 2800.0f;
    float wet_mv = 1200.0f;
};

// Returns 0..100 (0 = dry, 100 = wet), clamped. NaN if the calibration is unusable.
float moisturePercent(float mv, const SoilCalibration& cal);

// A reading stuck near 0 V or near the supply rail usually means the probe
// is unplugged or shorted.
bool sensorPlausible(float mv);

enum class State : uint8_t { Idle, Watering, Resting, TankEmpty, SensorFault };

struct Config {
    float start_below = 35.0f;      // start watering under this moisture %
    float stop_above = 55.0f;       // stop once moisture reaches this %
    uint32_t max_run_ms = 20000;    // never run the pump longer than this in one go
    uint32_t rest_ms = 60000;       // wait this long after a run before checking again
};

class Controller {
public:
    explicit Controller(const Config& cfg) : cfg_(cfg) {}

    // Call often. `now_ms` is millis(); `moisture` is in percent.
    void update(uint32_t now_ms, float moisture, bool sensor_ok, bool tank_ok);

    bool pumpOn() const { return state_ == State::Watering; }
    State state() const { return state_; }
    uint32_t runs() const { return runs_; }
    const Config& config() const { return cfg_; }

    // Both return false (and change nothing) if the result would not leave
    // start_below < stop_above inside 0..100.
    bool setStartBelow(float v);
    bool setStopAbove(float v);

private:
    Config cfg_;
    State state_ = State::Idle;
    uint32_t since_ms_ = 0;
    uint32_t runs_ = 0;
};

const char* toString(State s);

}  // namespace irrigation
