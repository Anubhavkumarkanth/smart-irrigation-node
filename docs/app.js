(function () {
  'use strict';

  const { State, Controller, SensorChain, defaultCalibration } = window.Irrigation;

  const STEP_MS = 500;            // the firmware senses every 500 ms
  const WINDOW_SAMPLES = 600;     // 5 simulated minutes of history
  const PUMP_GAIN = 1.5;          // % per second while the pump runs
  const DRYING = 0.3;             // % per second lost by the soil
  const NOISE_MV = 6;
  const MAX_RUN_S = 20;
  const REST_S = 60;

  const $ = (id) => document.getElementById(id);
  const el = {
    badge: $('badge'), badgeText: $('badgeText'), led: $('led'), pumpText: $('pumpText'),
    explain: $('explain'), vMoist: $('vMoist'), vMv: $('vMv'), vRuns: $('vRuns'), vClock: $('vClock'),
    chart: $('chart'), log: $('log'), logEmpty: $('logEmpty'),
    soil: $('soil'), soilOut: $('soilOut'), tank: $('tank'), sensor: $('sensor'),
    startBelow: $('startBelow'), startOut: $('startOut'), stopAbove: $('stopAbove'), stopOut: $('stopOut'),
    msg: $('msg'), speed: $('speed'), pause: $('pause'), reset: $('reset'),
  };

  const LABELS = {
    idle: 'Idle',
    watering: 'Watering',
    resting: 'Resting',
    tank_empty: 'Tank empty',
    sensor_fault: 'Sensor fault',
  };

  let ctrl, chain, soil, simMs, history, logItems, msgTimer;
  let paused = false;

  function reset() {
    ctrl = new Controller();
    chain = new SensorChain(defaultCalibration);
    soil = 30;
    simMs = 0;
    history = [];
    logItems = [];
    el.soil.value = soil;
    el.tank.checked = true;
    el.sensor.checked = true;
    el.startBelow.value = ctrl.cfg.startBelow;
    el.stopAbove.value = ctrl.cfg.stopAbove;
    el.log.innerHTML = '';
    el.logEmpty.hidden = false;
    setMsg('');
    syncLabels();
    render();
  }

  function fmtClock(ms) {
    const s = Math.floor(ms / 1000);
    return String(Math.floor(s / 60)).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
  }

  function setMsg(text) {
    el.msg.textContent = text;
    clearTimeout(msgTimer);
    if (text) msgTimer = setTimeout(() => { el.msg.textContent = ''; }, 5000);
  }

  function syncLabels() {
    el.soilOut.textContent = Math.round(soil) + '%';
    el.startOut.textContent = ctrl.cfg.startBelow + '%';
    el.stopOut.textContent = ctrl.cfg.stopAbove + '%';
  }

  function reasonFor(prev, next, moisture) {
    const c = ctrl.cfg;
    const m = moisture.toFixed(2) + '%';
    if (next === State.TankEmpty) return 'float switch reports an empty tank, pump forced off';
    if (next === State.SensorFault) return 'sensor reading is implausible, pump forced off';
    if (prev === State.TankEmpty) return 'tank refilled, back to normal';
    if (prev === State.SensorFault) return 'sensor reading is plausible again';
    if (prev === State.Idle && next === State.Watering) return 'moisture ' + m + ' is below ' + c.startBelow + '%';
    if (prev === State.Watering && next === State.Resting) {
      return moisture >= c.stopAbove
        ? 'moisture ' + m + ' reached ' + c.stopAbove + '%'
        : 'hit the ' + MAX_RUN_S + ' s maximum run time';
    }
    if (prev === State.Resting && next === State.Idle) return REST_S + ' s rest is over';
    return '';
  }

  function addLog(text) {
    logItems.unshift({ t: simMs, text });
    if (logItems.length > 80) logItems.pop();
    const li = document.createElement('li');
    const time = document.createElement('time');
    time.textContent = fmtClock(simMs);
    const span = document.createElement('span');
    span.textContent = text;
    li.append(time, span);
    el.log.prepend(li);
    while (el.log.children.length > 80) el.log.lastChild.remove();
    el.logEmpty.hidden = true;
  }

  function step() {
    simMs += STEP_MS;

    const connected = el.sensor.checked;
    const span = defaultCalibration.dryMv - defaultCalibration.wetMv;
    const mv = connected
      ? defaultCalibration.dryMv - (soil / 100) * span + (Math.random() * 2 - 1) * NOISE_MV
      : 0;

    chain.read(mv);
    const prev = ctrl.state;
    ctrl.update(simMs, chain.moisture, chain.sensorOk && chain.haveReading, el.tank.checked);

    if (ctrl.state !== prev) {
      addLog(LABELS[prev] + ' to ' + LABELS[ctrl.state].toLowerCase() + ': ' + reasonFor(prev, ctrl.state, chain.moisture));
    }

    const dt = STEP_MS / 1000;
    soil += ((ctrl.pumpOn ? PUMP_GAIN : 0) - DRYING) * dt;
    soil = Math.max(0, Math.min(100, soil));

    history.push({ m: chain.sensorOk ? chain.moisture : null, pump: ctrl.pumpOn });
    if (history.length > WINDOW_SAMPLES) history.shift();
  }

  function explain() {
    const elapsed = ((simMs - ctrl.sinceMs) >>> 0) / 1000;
    switch (ctrl.state) {
      case State.Idle:
        return 'Soil is fine. Waiting for moisture to fall below ' + ctrl.cfg.startBelow + '%.';
      case State.Watering:
        return 'Pump is on. It stops at ' + ctrl.cfg.stopAbove + '% or after ' + MAX_RUN_S + ' s (' + Math.max(0, Math.ceil(MAX_RUN_S - elapsed)) + ' s left).';
      case State.Resting:
        return 'Pump is off while the water soaks in (' + Math.max(0, Math.ceil(REST_S - elapsed)) + ' s of rest left), then moisture is checked again.';
      case State.TankEmpty:
        return 'The float switch says the tank is empty, so the pump cannot run. It starts again when the tank is refilled.';
      case State.SensorFault:
        return 'The sensor reading is implausible (unplugged or shorted), so the pump cannot run. It recovers when the reading is plausible again.';
      default:
        return '';
    }
  }

  function cssVar(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  }

  function drawChart() {
    const canvas = el.chart;
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
    }
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);

    const padL = 34, padR = 10, padT = 8, padB = 18;
    const pw = w - padL - padR;
    const ph = h - padT - padB;
    const X = (i) => padL + (i / (WINDOW_SAMPLES - 1)) * pw;
    const Y = (v) => padT + (1 - v / 100) * ph;
    const text = cssVar('--muted');
    const grid = cssVar('--grid');

    ctx.font = '11px system-ui, sans-serif';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = text;
    ctx.strokeStyle = grid;
    ctx.lineWidth = 1;
    ctx.textAlign = 'right';
    for (let v = 0; v <= 100; v += 25) {
      ctx.beginPath();
      ctx.moveTo(padL, Y(v));
      ctx.lineTo(w - padR, Y(v));
      ctx.stroke();
      ctx.fillText(String(v), padL - 6, Y(v));
    }
    ctx.textAlign = 'center';
    ctx.textBaseline = 'alphabetic';
    ctx.fillText('older', padL + 16, h - 3);
    ctx.fillText('now', w - padR - 10, h - 3);

    // History is drawn right-aligned so "now" is always at the right edge.
    const offset = WINDOW_SAMPLES - history.length;

    // pump-on bands
    ctx.fillStyle = cssVar('--pump');
    let runStart = -1;
    for (let i = 0; i <= history.length; i++) {
      const on = i < history.length && history[i].pump;
      if (on && runStart < 0) runStart = i;
      if (!on && runStart >= 0) {
        ctx.fillRect(X(runStart + offset), padT, Math.max(2, X(i + offset) - X(runStart + offset)), ph);
        runStart = -1;
      }
    }

    // thresholds
    ctx.setLineDash([5, 4]);
    ctx.strokeStyle = text;
    ctx.lineWidth = 1.2;
    [[ctrl.cfg.startBelow, 'start'], [ctrl.cfg.stopAbove, 'stop']].forEach(([v, name]) => {
      ctx.beginPath();
      ctx.moveTo(padL, Y(v));
      ctx.lineTo(w - padR, Y(v));
      ctx.stroke();
      ctx.textAlign = 'left';
      ctx.textBaseline = 'bottom';
      ctx.fillStyle = text;
      ctx.fillText(name + ' ' + v + '%', padL + 4, Y(v) - 2);
    });
    ctx.setLineDash([]);

    // measured moisture
    ctx.strokeStyle = cssVar('--line');
    ctx.lineWidth = 2;
    ctx.lineJoin = 'round';
    ctx.beginPath();
    let pen = false;
    history.forEach((s, i) => {
      if (s.m === null) { pen = false; return; }
      const x = X(i + offset), y = Y(s.m);
      if (!pen) { ctx.moveTo(x, y); pen = true; } else { ctx.lineTo(x, y); }
    });
    ctx.stroke();
  }

  function render() {
    const s = ctrl.state;
    el.badge.dataset.state = s;
    el.badgeText.textContent = LABELS[s];
    el.led.classList.toggle('on', ctrl.pumpOn);
    el.pumpText.textContent = ctrl.pumpOn ? 'Pump ON' : 'Pump off';
    el.explain.textContent = explain();
    el.vMoist.textContent = chain.haveReading ? chain.moisture.toFixed(1) + '%' : '--';
    el.vMv.textContent = chain.sensorOk ? Math.round(chain.soilMv) + ' mV' : 'no signal';
    el.vRuns.textContent = String(ctrl.runs);
    el.vClock.textContent = fmtClock(simMs);
    if (document.activeElement !== el.soil) el.soil.value = Math.round(soil);
    syncLabels();
    drawChart();
  }

  // Controls
  el.soil.addEventListener('input', () => { soil = Number(el.soil.value); syncLabels(); });

  el.startBelow.addEventListener('input', () => {
    const v = Number(el.startBelow.value);
    if (ctrl.setStartBelow(v)) {
      setMsg('');
    } else {
      setMsg('Rejected: the start level must stay below the stop level (' + ctrl.cfg.stopAbove + '%).');
      el.startBelow.value = ctrl.cfg.startBelow;
    }
    syncLabels();
  });

  el.stopAbove.addEventListener('input', () => {
    const v = Number(el.stopAbove.value);
    if (ctrl.setStopAbove(v)) {
      setMsg('');
    } else {
      setMsg('Rejected: the stop level must stay above the start level (' + ctrl.cfg.startBelow + '%).');
      el.stopAbove.value = ctrl.cfg.stopAbove;
    }
    syncLabels();
  });

  el.pause.addEventListener('click', () => {
    paused = !paused;
    el.pause.textContent = paused ? 'Resume' : 'Pause';
  });
  el.reset.addEventListener('click', reset);
  window.addEventListener('resize', drawChart);

  reset();

  // Run the simulation in fixed 500 ms steps scaled by the speed setting.
  let last = performance.now();
  let acc = 0;
  function frame(ts) {
    const dt = Math.min(ts - last, 100);
    last = ts;
    if (!paused) {
      acc += dt * Number(el.speed.value);
      let n = 0;
      while (acc >= STEP_MS && n < 200) { step(); acc -= STEP_MS; n++; }
      if (n >= 200) acc = 0;
    }
    render();
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
})();
