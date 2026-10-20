#include "irrigation.h"

#include <cmath>
#include <limits>

namespace irrigation {

float moisturePercent(float mv, const SoilCalibration& cal) {
    const float span = cal.dry_mv - cal.wet_mv;
    if (std::fabs(span) < 1.0f) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    float pct = (cal.dry_mv - mv) / span * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

bool sensorPlausible(float mv) {
    return std::isfinite(mv) && mv > 100.0f && mv < 3200.0f;
}

bool Controller::setStartBelow(float v) {
    if (!(v >= 0.0f && v < cfg_.stop_above)) return false;
    cfg_.start_below = v;
    return true;
}

bool Controller::setStopAbove(float v) {
    if (!(v <= 100.0f && v > cfg_.start_below)) return false;
    cfg_.stop_above = v;
    return true;
}

void Controller::update(uint32_t now_ms, float moisture, bool sensor_ok, bool tank_ok) {
    // Safety conditions come first and always switch the pump off.
    if (!sensor_ok || std::isnan(moisture)) {
        state_ = State::SensorFault;
        return;
    }
    if (!tank_ok) {
        state_ = State::TankEmpty;
        return;
    }
    if (state_ == State::SensorFault || state_ == State::TankEmpty) {
        state_ = State::Idle;
    }

    const uint32_t elapsed = now_ms - since_ms_;  // unsigned, so it survives millis() wrapping

    switch (state_) {
        case State::Idle:
            if (moisture < cfg_.start_below) {
                state_ = State::Watering;
                since_ms_ = now_ms;
                ++runs_;
            }
            break;
        case State::Watering:
            if (moisture >= cfg_.stop_above || elapsed >= cfg_.max_run_ms) {
                state_ = State::Resting;
                since_ms_ = now_ms;
            }
            break;
        case State::Resting:
            if (elapsed >= cfg_.rest_ms) {
                state_ = State::Idle;
            }
            break;
        default:
            break;
    }
}

const char* toString(State s) {
    switch (s) {
        case State::Idle:        return "idle";
        case State::Watering:    return "watering";
        case State::Resting:     return "resting";
        case State::TankEmpty:   return "tank_empty";
        case State::SensorFault: return "sensor_fault";
    }
    return "unknown";
}

}  // namespace irrigation
