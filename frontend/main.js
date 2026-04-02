/**
 * main.js – Application Controller for ATC Simulator (v2)
 *
 * Improvements:
 *   - Welcome/help overlay with tutorial
 *   - Quick-action buttons (heading, altitude, speed)
 *   - Hover tooltips on aircraft
 *   - Mouse-wheel zoom
 *   - Keyboard shortcuts (Esc to deselect, ? for help)
 *   - Clickable command examples that auto-fill
 *   - Better command feedback with confirmation
 *   - Altitude trend indicators
 */

/* ══════════════════════════════════════════════════════════════════════
 *  Initialisation
 * ══════════════════════════════════════════════════════════════════════ */

const canvas = document.getElementById('radarCanvas');
const radar  = new RadarDisplay(canvas);

let latestTelemetry = null;

/* ── Network ─────────────────────────────────────────────────────────── */

const network = new NetworkManager(
    (data) => {
        latestTelemetry = data;
        radar.aircraft  = data.aircraft || [];
        radar.conflicts = data.conflicts || [];
        updateStatusBar(data);
        updateInfoPanel();
        updateConflictList(data.conflicts || []);
    },
    (status) => {
        const el = document.getElementById('connectionStatus');
        const labels = { connected: 'CONNECTED', disconnected: 'DISCONNECTED', error: 'ERROR' };
        el.textContent = labels[status] || status.toUpperCase();
        el.className = 'status-' + status;
        document.getElementById('commandInput').disabled = (status !== 'connected');
    }
);

network.connect(`ws://${window.location.host}`);

/* ══════════════════════════════════════════════════════════════════════
 *  Animation loop
 * ══════════════════════════════════════════════════════════════════════ */

let lastFrameTime = 0;

function animate(timestamp) {
    const dt = Math.min((timestamp - lastFrameTime) / 1000, 0.1);
    lastFrameTime = timestamp;
    radar.render(dt);
    requestAnimationFrame(animate);
}

requestAnimationFrame(animate);

/* ══════════════════════════════════════════════════════════════════════
 *  Window resize
 * ══════════════════════════════════════════════════════════════════════ */

window.addEventListener('resize', () => radar.resize());

/* ══════════════════════════════════════════════════════════════════════
 *  Help overlay
 * ══════════════════════════════════════════════════════════════════════ */

const helpOverlay = document.getElementById('helpOverlay');

function showHelp() {
    helpOverlay.classList.remove('closing', 'hidden');
    helpOverlay.style.display = 'flex';
}

function hideHelp() {
    helpOverlay.classList.add('closing');
    setTimeout(() => {
        helpOverlay.style.display = 'none';
        helpOverlay.classList.remove('closing');
    }, 280);
}

document.getElementById('helpCloseBtn').addEventListener('click', hideHelp);
document.getElementById('helpBtn').addEventListener('click', () => {
    if (helpOverlay.style.display === 'none') showHelp();
    else hideHelp();
});

// Click outside card to close
helpOverlay.addEventListener('click', (e) => {
    if (e.target === helpOverlay) hideHelp();
});

/* ══════════════════════════════════════════════════════════════════════
 *  Keyboard shortcuts
 * ══════════════════════════════════════════════════════════════════════ */

document.addEventListener('keydown', (e) => {
    // Don't capture if typing in input
    if (e.target.tagName === 'INPUT') {
        if (e.key === 'Escape') {
            e.target.blur();
            deselectAircraft();
        }
        return;
    }

    switch (e.key) {
        case '?':
            e.preventDefault();
            if (helpOverlay.style.display === 'none') showHelp();
            else hideHelp();
            break;
        case 'Escape':
            if (helpOverlay.style.display !== 'none') {
                hideHelp();
            } else {
                deselectAircraft();
            }
            break;
        case '+':
        case '=':
            e.preventDefault();
            radar.setZoom(radar.zoom + 0.2);
            updateZoomDisplay();
            break;
        case '-':
        case '_':
            e.preventDefault();
            radar.setZoom(radar.zoom - 0.2);
            updateZoomDisplay();
            break;
    }
});

/* ══════════════════════════════════════════════════════════════════════
 *  Mouse wheel → zoom
 * ══════════════════════════════════════════════════════════════════════ */

canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    const delta = e.deltaY > 0 ? -0.15 : 0.15;
    radar.setZoom(radar.zoom + delta);
    updateZoomDisplay();
}, { passive: false });

function updateZoomDisplay() {
    document.getElementById('zoomLevel').textContent =
        `ZOOM: ${radar.zoom.toFixed(1)}×`;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Canvas click → aircraft selection
 * ══════════════════════════════════════════════════════════════════════ */

canvas.addEventListener('click', (e) => {
    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    const cs = radar.handleClick(x, y);

    if (cs) {
        selectAircraft(cs);
    } else {
        deselectAircraft();
    }
});

function selectAircraft(cs) {
    radar.selectedCallsign = cs;
    document.getElementById('noSelection').classList.add('hidden');
    document.getElementById('selectionInfo').classList.remove('hidden');
    document.getElementById('quickActions').classList.remove('hidden');
    document.getElementById('commandInput').disabled = false;
    document.getElementById('commandInput').placeholder = `Command for ${cs}...`;
    document.getElementById('commandInput').focus();
    updateInfoPanel();
    showFeedback(`Selected ${cs}`, 'success');
}

function deselectAircraft() {
    radar.selectedCallsign = null;
    document.getElementById('noSelection').classList.remove('hidden');
    document.getElementById('selectionInfo').classList.add('hidden');
    document.getElementById('quickActions').classList.add('hidden');
    document.getElementById('commandInput').placeholder = 'Select an aircraft first...';
    showFeedback('', '');
}

/* ══════════════════════════════════════════════════════════════════════
 *  Hover tooltip
 * ══════════════════════════════════════════════════════════════════════ */

const tooltip = document.getElementById('hoverTooltip');

canvas.addEventListener('mousemove', (e) => {
    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    radar.updateHover(x, y);
    const data = radar.getHoveredAircraftData();

    if (data && data.cs !== radar.selectedCallsign) {
        const altFL = Math.round(data.alt / 100);
        const trend = (data.talt > data.alt + 50) ? ' ↑'
                    : (data.talt < data.alt - 50) ? ' ↓' : '';

        tooltip.innerHTML = `
            <div class="tt-cs">${data.cs}</div>
            <div class="tt-row">ALT: <span class="tt-val">FL${altFL}${trend}</span></div>
            <div class="tt-row">SPD: <span class="tt-val">${Math.round(data.spd)} kts</span></div>
            <div class="tt-row">HDG: <span class="tt-val">${Math.round(data.hdg)}°</span></div>
        `;

        // Position tooltip near cursor, offset to avoid overlap
        let tx = e.clientX - rect.left + 18;
        let ty = e.clientY - rect.top - 10;

        // Keep on-screen
        if (tx + 140 > rect.width) tx = e.clientX - rect.left - 150;
        if (ty + 80 > rect.height) ty = e.clientY - rect.top - 80;

        tooltip.style.left = tx + 'px';
        tooltip.style.top = ty + 'px';
        tooltip.classList.remove('hidden');
        tooltip.style.display = 'block';
        tooltip.style.opacity = '1';
        canvas.style.cursor = 'pointer';
    } else {
        tooltip.classList.add('hidden');
        tooltip.style.display = 'none';
        canvas.style.cursor = 'crosshair';
    }
});

canvas.addEventListener('mouseleave', () => {
    tooltip.classList.add('hidden');
    tooltip.style.display = 'none';
    radar.hoveredCallsign = null;
});

/* ══════════════════════════════════════════════════════════════════════
 *  Command input
 * ══════════════════════════════════════════════════════════════════════ */

document.getElementById('commandInput').addEventListener('keydown', (e) => {
    if (e.key !== 'Enter') return;

    const input = e.target.value.trim().toLowerCase();
    if (!input) return;

    const cs = radar.selectedCallsign;
    if (!cs) {
        showFeedback('No aircraft selected', 'error');
        return;
    }

    const parsed = parseCommand(input);
    if (!parsed) {
        showFeedback('Unknown command. Try: heading 270, altitude 350, speed 250', 'error');
        return;
    }

    const sent = network.sendCommand(cs, parsed.action, parsed.value);
    if (sent) {
        const label = parsed.action === 'heading' ? `heading ${parsed.value}°`
                    : parsed.action === 'altitude' ? `FL${Math.round(parsed.value/100)}`
                    : `${parsed.value} kts`;
        showFeedback(`✓ ${cs}: ${parsed.action} → ${label}`, 'success');
        e.target.value = '';
    } else {
        showFeedback('Not connected to server', 'error');
    }
});

/* ── Clickable command examples ──────────────────────────────────────── */

document.querySelectorAll('.command-examples code').forEach(el => {
    el.addEventListener('click', () => {
        const input = document.getElementById('commandInput');
        if (!input.disabled) {
            input.value = el.textContent;
            input.focus();
        }
    });
});

/* ══════════════════════════════════════════════════════════════════════
 *  Quick-action buttons
 * ══════════════════════════════════════════════════════════════════════ */

document.querySelectorAll('.quick-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        const cs = radar.selectedCallsign;
        if (!cs) return;

        const action = btn.dataset.action;
        const delta  = btn.dataset.delta;
        let value    = parseFloat(btn.dataset.value || 0);

        // Handle relative deltas for altitude/speed
        if (delta && latestTelemetry) {
            const ac = latestTelemetry.aircraft.find(a => a.cs === cs);
            if (!ac) return;

            const d = parseFloat(delta);
            if (action === 'altitude') {
                value = Math.max(1000, Math.min(45000, ac.alt + d));
            } else if (action === 'speed') {
                value = Math.max(150, Math.min(550, ac.spd + d));
            }
        }

        // For heading value=360, normalize to 360 (displayed as N)
        if (action === 'heading' && value === 360) value = 360;

        const sent = network.sendCommand(cs, action, value);
        if (sent) {
            const label = action === 'heading' ? `heading ${value}°`
                        : action === 'altitude' ? `FL${Math.round(value/100)}`
                        : `${Math.round(value)} kts`;
            showFeedback(`✓ ${cs}: ${action} → ${label}`, 'success');
        }
    });
});

/* ══════════════════════════════════════════════════════════════════════
 *  Command parser
 * ══════════════════════════════════════════════════════════════════════ */

function parseCommand(input) {
    let match;

    // Heading
    match = input.match(/(?:heading|hdg|turn\s+\w+\s+heading)\s+(\d+)/);
    if (match) {
        let val = parseInt(match[1], 10);
        if (val >= 0 && val <= 360) return { action: 'heading', value: val };
    }

    // Altitude
    match = input.match(/(?:altitude|alt|climb|descend|fl)\s+(\d+)/);
    if (match) {
        let val = parseInt(match[1], 10);
        if (val > 0 && val < 1000) val *= 100;
        if (val >= 1000 && val <= 60000) return { action: 'altitude', value: val };
    }

    // Speed
    match = input.match(/(?:speed|spd)\s+(\d+)/);
    if (match) {
        let val = parseInt(match[1], 10);
        if (val >= 100 && val <= 600) return { action: 'speed', value: val };
    }

    return null;
}

/* ══════════════════════════════════════════════════════════════════════
 *  UI updates
 * ══════════════════════════════════════════════════════════════════════ */

function updateStatusBar(data) {
    const t = data.time || 0;
    const h = String(Math.floor(t / 3600)).padStart(2, '0');
    const m = String(Math.floor((t % 3600) / 60)).padStart(2, '0');
    const s = String(Math.floor(t % 60)).padStart(2, '0');
    document.getElementById('simTime').textContent = `T+ ${h}:${m}:${s}`;

    document.getElementById('acCount').textContent =
        `AC: ${(data.aircraft || []).length}`;

    const cc = (data.conflicts || []).length;
    const el = document.getElementById('conflictCount');
    el.textContent = `CONFLICTS: ${cc}`;
    el.className = cc > 0 ? 'has-conflict' : 'no-conflict';
}

function updateInfoPanel() {
    const cs = radar.selectedCallsign;
    if (!cs || !latestTelemetry) return;

    const ac = (latestTelemetry.aircraft || []).find(a => a.cs === cs);
    if (!ac) {
        // Aircraft left sector while selected
        deselectAircraft();
        return;
    }

    document.getElementById('infoCallsign').textContent = ac.cs;

    const altFL = Math.round(ac.alt / 100);
    let trend = '';
    if (ac.talt > ac.alt + 50) trend = ' ↑';
    else if (ac.talt < ac.alt - 50) trend = ' ↓';

    document.getElementById('infoAltitude').textContent =
        `FL${altFL}${trend}  (${Math.round(ac.alt).toLocaleString()} ft)`;
    document.getElementById('infoSpeed').textContent =
        `${Math.round(ac.spd)} kts`;
    document.getElementById('infoHeading').textContent =
        `${Math.round(ac.hdg)}°`;
    document.getElementById('infoTgtHdg').textContent =
        `${Math.round(ac.thdg)}°`;
    document.getElementById('infoTgtAlt').textContent =
        `FL${Math.round(ac.talt / 100)}`;
    document.getElementById('infoTgtSpd').textContent =
        `${Math.round(ac.tspd)} kts`;
}

function updateConflictList(conflicts) {
    const ul = document.getElementById('conflictList');
    ul.innerHTML = '';

    conflicts.forEach(c => {
        const li = document.createElement('li');
        const latText = c.lat !== undefined ? c.lat.toFixed(1) : '?';
        const vertText = c.vert !== undefined ? Math.round(c.vert) : '?';
        li.textContent = `${c.a} ↔ ${c.b}  (${latText}nm / ${vertText}ft)`;

        // Click conflict to select first aircraft
        li.style.cursor = 'pointer';
        li.addEventListener('click', () => selectAircraft(c.a));

        ul.appendChild(li);
    });
}

function showFeedback(msg, type) {
    const el = document.getElementById('commandFeedback');
    el.textContent = msg;
    el.className = type || '';
    if (msg) {
        clearTimeout(el._timer);
        el._timer = setTimeout(() => { el.textContent = ''; el.className = ''; }, 5000);
    }
}
