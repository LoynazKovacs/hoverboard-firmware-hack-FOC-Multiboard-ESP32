async function updateTelemetry() {
  try {
    const resp = await fetch('/telemetry');
    const j = await resp.json();

    document.getElementById('targetCmd').textContent = j.target;
    document.getElementById('rampedCmd').textContent = j.ramped;

    for (let i = 0; i < j.boards.length; i++) {
      const b = j.boards[i];
      document.getElementById('spd' + i).textContent = b.spdR + '/' + b.spdL;
      document.getElementById('vol' + i).textContent = b.V;
      document.getElementById('tmp' + i).textContent = b.T;

      const ind = document.getElementById('ind' + i);
      if (b.valid) {
        ind.classList.add('active');
      } else {
        ind.classList.remove('active');
      }
    }
  } catch (e) {
    console.error('Telemetry fetch error:', e);
  }
}

window.setInterval(updateTelemetry, 500);
updateTelemetry();
