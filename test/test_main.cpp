#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "irrigation.h"
#include "payload.h"

using namespace irrigation;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++checks;                                                         \
        if (!(cond)) {                                                    \
            ++failures;                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);           \
        }                                                                 \
    } while (0)

static const unsigned char* bytes(const char* s) {
    return reinterpret_cast<const unsigned char*>(s);
}

static void moistureConversion() {
    SoilCalibration cal;  // dry 2800 mV, wet 1200 mV
    CHECK(std::fabs(moisturePercent(2800.0f, cal) - 0.0f) < 0.01f);
    CHECK(std::fabs(moisturePercent(1200.0f, cal) - 100.0f) < 0.01f);
    CHECK(std::fabs(moisturePercent(2000.0f, cal) - 50.0f) < 0.01f);
    // outside the calibrated range gets clamped
    CHECK(moisturePercent(3000.0f, cal) == 0.0f);
    CHECK(moisturePercent(900.0f, cal) == 100.0f);
    // wetter soil means lower voltage
    CHECK(moisturePercent(1500.0f, cal) > moisturePercent(2500.0f, cal));
    SoilCalibration broken{2000.0f, 2000.0f};
    CHECK(std::isnan(moisturePercent(2000.0f, broken)));
}

static void plausibleReadings() {
    CHECK(sensorPlausible(1800.0f));
    CHECK(!sensorPlausible(0.0f));
    CHECK(!sensorPlausible(3300.0f));
    CHECK(!sensorPlausible(std::numeric_limits<float>::quiet_NaN()));
}

static Config shortCfg() {
    Config c;
    c.start_below = 35.0f;
    c.stop_above = 55.0f;
    c.max_run_ms = 20000;
    c.rest_ms = 60000;
    return c;
}

static void staysIdleInsideBand() {
    Controller c(shortCfg());
    c.update(1000, 40.0f, true, true);
    CHECK(c.state() == State::Idle && !c.pumpOn());
    c.update(2000, 90.0f, true, true);
    CHECK(c.state() == State::Idle && !c.pumpOn());
}

static void waterUntilWetEnough() {
    Controller c(shortCfg());
    c.update(1000, 30.0f, true, true);
    CHECK(c.pumpOn() && c.runs() == 1);
    c.update(5000, 45.0f, true, true);  // between thresholds: keep going
    CHECK(c.pumpOn());
    c.update(9000, 56.0f, true, true);  // reached the stop level
    CHECK(c.state() == State::Resting && !c.pumpOn());
}

static void maxRunTimeStopsPump() {
    Controller c(shortCfg());
    c.update(0, 10.0f, true, true);
    CHECK(c.pumpOn());
    c.update(19999, 10.0f, true, true);
    CHECK(c.pumpOn());
    c.update(20000, 10.0f, true, true);
    CHECK(c.state() == State::Resting && !c.pumpOn());
}

static void restPeriodBeforeNextRun() {
    Controller c(shortCfg());
    c.update(0, 10.0f, true, true);
    c.update(20000, 10.0f, true, true);  // now resting
    c.update(50000, 10.0f, true, true);  // 30 s into the rest, still dry
    CHECK(c.state() == State::Resting && !c.pumpOn());
    c.update(80000, 10.0f, true, true);  // rest over
    CHECK(c.state() == State::Idle);
    c.update(80500, 10.0f, true, true);
    CHECK(c.pumpOn() && c.runs() == 2);
}

static void emptyTankStopsPumpAndRecovers() {
    Controller c(shortCfg());
    c.update(0, 10.0f, true, true);
    CHECK(c.pumpOn());
    c.update(3000, 10.0f, true, false);  // float switch says empty
    CHECK(c.state() == State::TankEmpty && !c.pumpOn());
    c.update(4000, 10.0f, true, false);
    CHECK(!c.pumpOn());
    c.update(5000, 10.0f, true, true);  // refilled and the soil is still dry
    CHECK(c.pumpOn());
}

static void badSensorStopsPump() {
    Controller c(shortCfg());
    c.update(0, 10.0f, true, true);
    CHECK(c.pumpOn());
    c.update(1000, 0.0f, false, true);
    CHECK(c.state() == State::SensorFault && !c.pumpOn());
    c.update(2000, std::numeric_limits<float>::quiet_NaN(), true, true);
    CHECK(c.state() == State::SensorFault && !c.pumpOn());
    c.update(3000, 50.0f, true, true);  // sensor back, soil is fine
    CHECK(c.state() == State::Idle);
}

static void millisWrap() {
    Controller c(shortCfg());
    const uint32_t start = 0xFFFFFFFFu - 4999u;
    c.update(start, 10.0f, true, true);
    CHECK(c.pumpOn());
    c.update(start + 10000u, 10.0f, true, true);  // counter has wrapped, 10 s in
    CHECK(c.pumpOn());
    c.update(start + 20000u, 10.0f, true, true);
    CHECK(c.state() == State::Resting);
}

static void thresholdSetters() {
    Controller c(shortCfg());
    CHECK(c.setStartBelow(30.0f) && c.config().start_below == 30.0f);
    CHECK(!c.setStartBelow(60.0f));   // not below stop_above (55)
    CHECK(!c.setStartBelow(-1.0f));
    CHECK(c.config().start_below == 30.0f);
    CHECK(c.setStopAbove(70.0f) && c.config().stop_above == 70.0f);
    CHECK(!c.setStopAbove(20.0f));    // not above start_below (30)
    CHECK(!c.setStopAbove(101.0f));
    CHECK(c.config().stop_above == 70.0f);
}

static void payloadParsing() {
    float v = 0.0f;
    CHECK(parsePercent(bytes("42.5"), 4, &v) && std::fabs(v - 42.5f) < 1e-6f);
    CHECK(parsePercent(bytes("40garbage"), 2, &v) && v == 40.0f);  // length is respected
    CHECK(!parsePercent(bytes("40garbage"), 9, &v));
    CHECK(!parsePercent(bytes(""), 0, &v));
    CHECK(!parsePercent(bytes("abc"), 3, &v));
    CHECK(!parsePercent(bytes("101"), 3, &v));
    CHECK(!parsePercent(bytes("-5"), 2, &v));
    CHECK(!parsePercent(bytes("nan"), 3, &v));
    CHECK(!parsePercent(bytes("inf"), 3, &v));
    CHECK(!parsePercent(nullptr, 2, &v));
    CHECK(!parsePercent(bytes("1234567890123456"), 16, &v));
}

int main() {
    moistureConversion();
    plausibleReadings();
    staysIdleInsideBand();
    waterUntilWetEnough();
    maxRunTimeStopsPump();
    restPeriodBeforeNextRun();
    emptyTankStopsPumpAndRecovers();
    badSensorStopsPump();
    millisWrap();
    thresholdSetters();
    payloadParsing();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
