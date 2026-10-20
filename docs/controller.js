// JavaScript port of lib/irrigation/irrigation.cpp and the sensor path in src/main.cpp.
// Values go through Math.fround so comparisons behave like the firmware's 32-bit floats.
(function (root) {
  'use strict';

  const fr = Math.fround;

  const State = Object.freeze({
    Idle: 'idle',
    Watering: 'watering',
    Resting: 'resting',
    TankEmpty: 'tank_empty',
    SensorFault: 'sensor_fault',
  });

  const defaultCalibration = { dryMv: 2800, wetMv: 1200 };
  const defaultConfig = { startBelow: 35, stopAbove: 55, maxRunMs: 20000, restMs: 60000 };

  // 0 = dry, 100 = wet, clamped. NaN if the calibration is unusable.
  function moisturePercent(mv, cal) {
    mv = fr(mv);
    const span = fr(cal.dryMv - cal.wetMv);
    if (Math.abs(span) < 1) return NaN;
    let pct = fr(fr(fr(cal.dryMv - mv) / span) * 100);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
  }

  function sensorPlausible(mv) {
    return Number.isFinite(mv) && mv > 100 && mv < 3200;
  }

  class Controller {
    constructor(cfg) {
      const c = Object.assign({}, defaultConfig, cfg || {});
      this.cfg = {
        startBelow: fr(c.startBelow),
        stopAbove: fr(c.stopAbove),
        maxRunMs: c.maxRunMs >>> 0,
        restMs: c.restMs >>> 0,
      };
      this.state = State.Idle;
      this.sinceMs = 0;
      this.runs = 0;
    }

    get pumpOn() {
      return this.state === State.Watering;
    }

    setStartBelow(v) {
      v = fr(v);
      if (!(v >= 0 && v < this.cfg.stopAbove)) return false;
      this.cfg.startBelow = v;
      return true;
    }

    setStopAbove(v) {
      v = fr(v);
      if (!(v <= 100 && v > this.cfg.startBelow)) return false;
      this.cfg.stopAbove = v;
      return true;
    }

    update(nowMs, moisture, sensorOk, tankOk) {
      nowMs = nowMs >>> 0;
      moisture = fr(moisture);

      if (!sensorOk || Number.isNaN(moisture)) {
        this.state = State.SensorFault;
        return;
      }
      if (!tankOk) {
        this.state = State.TankEmpty;
        return;
      }
      if (this.state === State.SensorFault || this.state === State.TankEmpty) {
        this.state = State.Idle;
      }

      const elapsed = (nowMs - this.sinceMs) >>> 0;

      switch (this.state) {
        case State.Idle:
          if (moisture < this.cfg.startBelow) {
            this.state = State.Watering;
            this.sinceMs = nowMs;
            this.runs++;
          }
          break;
        case State.Watering:
          if (moisture >= this.cfg.stopAbove || elapsed >= this.cfg.maxRunMs) {
            this.state = State.Resting;
            this.sinceMs = nowMs;
          }
          break;
        case State.Resting:
          if (elapsed >= this.cfg.restMs) {
            this.state = State.Idle;
          }
          break;
        default:
          break;
      }
    }
  }

  const EMA_ALPHA = fr(0.3);

  // Sensor path from main.cpp: plausibility check, light smoothing, conversion.
  class SensorChain {
    constructor(cal) {
      this.cal = cal || defaultCalibration;
      this.soilMv = 0;
      this.moisture = 0;
      this.sensorOk = false;
      this.haveReading = false;
    }

    read(mv) {
      mv = fr(mv);
      this.sensorOk = sensorPlausible(mv);
      if (this.sensorOk) {
        this.soilMv = this.haveReading ? fr(this.soilMv + fr(EMA_ALPHA * fr(mv - this.soilMv))) : fr(mv);
        this.haveReading = true;
        this.moisture = moisturePercent(this.soilMv, this.cal);
      }
    }

    get usable() {
      return this.sensorOk && this.haveReading;
    }
  }

  const api = { State, Controller, SensorChain, moisturePercent, sensorPlausible, defaultCalibration, defaultConfig };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.Irrigation = api;
})(typeof window !== 'undefined' ? window : globalThis);
