/**
 * radar.js – Canvas Radar Display for ATC Simulator (v2)
 *
 * Improvements over v1:
 *   - Mouse-wheel zoom (0.5× to 3.0×)
 *   - Hover detection with tooltip data
 *   - Smoother sweep trail using conic gradient
 *   - Better data block positioning to reduce overlap
 *   - Altitude trend arrows (climbing ↑, descending ↓, level -)
 *   - Target heading indicator line (dashed)
 *
 * Coordinate System:
 *   Equirectangular projection centred on sector centre.
 *   1° lat ≈ 60 nm; 1° lon ≈ 60·cos(lat) nm.
 */

class RadarDisplay {
    constructor(canvas) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');

        /* ── Sector geography ────────────────────────────────────── */
        this.centerLat = 40.6413;
        this.centerLon = -73.7781;
        this.baseRangeNm = 60;
        this.rangeNm = 60;

        /* ── Zoom ────────────────────────────────────────────────── */
        this.zoom = 1.0;
        this.minZoom = 0.5;
        this.maxZoom = 3.0;

        /* ── Waypoint fixes ──────────────────────────────────────── */
        this.waypoints = [
            { name: 'JFK',   lat: 40.6413, lon: -73.7781 },
            { name: 'LGA',   lat: 40.7769, lon: -73.8740 },
            { name: 'EWR',   lat: 40.6895, lon: -74.1745 },
            { name: 'MERIT', lat: 41.00,   lon: -73.50   },
            { name: 'GREKI', lat: 40.30,   lon: -73.30   },
            { name: 'BETTE', lat: 40.80,   lon: -74.30   },
            { name: 'DIXIE', lat: 40.20,   lon: -74.00   },
            { name: 'CAMRN', lat: 41.10,   lon: -74.00   },
        ];

        /* ── Sweep animation ─────────────────────────────────────── */
        this.sweepAngle = 0;
        this.sweepSpeed = (2 * Math.PI) / 10;

        /* ── Live data (set externally) ──────────────────────────── */
        this.aircraft  = [];
        this.conflicts = [];

        /* ── Interaction state ───────────────────────────────────── */
        this.selectedCallsign = null;
        this.hoveredCallsign  = null;
        this.mouseX = -1;
        this.mouseY = -1;

        /* ── Derived on resize ───────────────────────────────────── */
        this.width = 0;
        this.height = 0;
        this.cx = 0;
        this.cy = 0;
        this.scale = 1;

        this.resize();
    }

    /* ══════════════════════════════════════════════════════════════
     *  Coordinate conversion
     * ══════════════════════════════════════════════════════════════ */

    latLonToScreen(lat, lon) {
        const nmPerDegLat = 60;
        const nmPerDegLon = 60 * Math.cos(this.centerLat * Math.PI / 180);
        const dx =  (lon - this.centerLon) * nmPerDegLon * this.scale;
        const dy = -(lat - this.centerLat) * nmPerDegLat * this.scale;
        return { x: this.cx + dx, y: this.cy + dy };
    }

    resize() {
        const container = this.canvas.parentElement;
        const dpr = window.devicePixelRatio || 1;

        this.width  = container.clientWidth;
        this.height = container.clientHeight;

        this.canvas.width  = this.width * dpr;
        this.canvas.height = this.height * dpr;
        this.canvas.style.width  = this.width + 'px';
        this.canvas.style.height = this.height + 'px';

        this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

        this.cx = this.width / 2;
        this.cy = this.height / 2;
        this.updateScale();
    }

    setZoom(z) {
        this.zoom = Math.max(this.minZoom, Math.min(this.maxZoom, z));
        this.rangeNm = this.baseRangeNm / this.zoom;
        this.updateScale();
    }

    updateScale() {
        this.scale = Math.min(this.width, this.height) / (2 * this.rangeNm);
    }

    /* ══════════════════════════════════════════════════════════════
     *  Hover detection
     * ══════════════════════════════════════════════════════════════ */

    updateHover(screenX, screenY) {
        this.mouseX = screenX;
        this.mouseY = screenY;

        const HIT_RADIUS = 20;
        let closest = null;
        let closestDist = Infinity;

        this.aircraft.forEach(ac => {
            const p = this.latLonToScreen(ac.lat, ac.lon);
            const dist = Math.hypot(screenX - p.x, screenY - p.y);
            if (dist < HIT_RADIUS && dist < closestDist) {
                closest = ac.cs;
                closestDist = dist;
            }
        });

        this.hoveredCallsign = closest;
        return closest;
    }

    /** Get hovered aircraft data for tooltip. */
    getHoveredAircraftData() {
        if (!this.hoveredCallsign) return null;
        const ac = this.aircraft.find(a => a.cs === this.hoveredCallsign);
        if (!ac) return null;
        const p = this.latLonToScreen(ac.lat, ac.lon);
        return { ...ac, screenX: p.x, screenY: p.y };
    }

    /* ══════════════════════════════════════════════════════════════
     *  Main render (called every animation frame)
     * ══════════════════════════════════════════════════════════════ */

    render(dt) {
        const ctx = this.ctx;

        // Background with subtle radial gradient
        ctx.fillStyle = '#030806';
        ctx.fillRect(0, 0, this.width, this.height);

        const grad = ctx.createRadialGradient(
            this.cx, this.cy, 0,
            this.cx, this.cy, Math.min(this.width, this.height) * 0.7);
        grad.addColorStop(0, 'rgba(0, 25, 5, 0.3)');
        grad.addColorStop(1, 'rgba(0, 0, 0, 0.6)');
        ctx.fillStyle = grad;
        ctx.fillRect(0, 0, this.width, this.height);

        // Clip to visible area for performance
        this.drawRangeRings(ctx);
        this.drawCompassRose(ctx);
        this.drawWaypoints(ctx);
        this.drawSweepLine(ctx, dt);
        this.drawHistoryTrails(ctx);
        this.drawConflictHighlights(ctx);
        this.drawAircraft(ctx);
    }

    /* ══════════════════════════════════════════════════════════════
     *  Range rings
     * ══════════════════════════════════════════════════════════════ */

    drawRangeRings(ctx) {
        const step = this.zoom >= 2 ? 5 : 10;
        ctx.font = '10px "JetBrains Mono", monospace';
        ctx.textAlign = 'left';
        ctx.textBaseline = 'top';

        for (let r = step; r <= this.rangeNm * 1.1; r += step) {
            const px = r * this.scale;
            // Rings
            ctx.strokeStyle = 'rgba(0, 255, 65, 0.08)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.arc(this.cx, this.cy, px, 0, Math.PI * 2);
            ctx.stroke();

            // Labels
            ctx.fillStyle = 'rgba(0, 255, 65, 0.22)';
            ctx.fillText(r + ' nm', this.cx + 4, this.cy - px + 3);
        }
    }

    /* ══════════════════════════════════════════════════════════════
     *  Compass rose
     * ══════════════════════════════════════════════════════════════ */

    drawCompassRose(ctx) {
        const labels = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
        const outerR = this.rangeNm * this.scale + 18;

        ctx.font = '11px "Inter", sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';

        labels.forEach((label, i) => {
            const isCardinal = i % 2 === 0;
            const angle = (i * 45 - 90) * Math.PI / 180;
            const x = this.cx + Math.cos(angle) * outerR;
            const y = this.cy + Math.sin(angle) * outerR;

            ctx.fillStyle = isCardinal
                ? 'rgba(0, 255, 65, 0.45)'
                : 'rgba(0, 255, 65, 0.2)';
            ctx.font = isCardinal
                ? 'bold 12px "Inter", sans-serif'
                : '10px "Inter", sans-serif';
            ctx.fillText(label, x, y);
        });

        // Radial lines
        ctx.strokeStyle = 'rgba(0, 255, 65, 0.04)';
        ctx.lineWidth = 1;
        for (let i = 0; i < 36; i++) {
            const angle = (i * 10 - 90) * Math.PI / 180;
            const inner = (i % 9 === 0) ? 0 : this.rangeNm * this.scale * 0.92;
            ctx.beginPath();
            ctx.moveTo(
                this.cx + Math.cos(angle) * inner,
                this.cy + Math.sin(angle) * inner);
            ctx.lineTo(
                this.cx + Math.cos(angle) * this.rangeNm * this.scale,
                this.cy + Math.sin(angle) * this.rangeNm * this.scale);
            ctx.stroke();
        }

        ctx.textAlign = 'start';
        ctx.textBaseline = 'alphabetic';
    }

    /* ══════════════════════════════════════════════════════════════
     *  Waypoint fixes
     * ══════════════════════════════════════════════════════════════ */

    drawWaypoints(ctx) {
        ctx.font = '10px "JetBrains Mono", monospace';

        this.waypoints.forEach(wp => {
            const p = this.latLonToScreen(wp.lat, wp.lon);

            // Skip if off-screen
            if (p.x < -20 || p.x > this.width + 20 ||
                p.y < -20 || p.y > this.height + 20) return;

            const s = 5;

            ctx.strokeStyle = 'rgba(0, 204, 51, 0.5)';
            ctx.fillStyle   = 'rgba(0, 204, 51, 0.1)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(p.x, p.y - s);
            ctx.lineTo(p.x - s, p.y + s);
            ctx.lineTo(p.x + s, p.y + s);
            ctx.closePath();
            ctx.fill();
            ctx.stroke();

            ctx.fillStyle = 'rgba(0, 204, 51, 0.55)';
            ctx.fillText(wp.name, p.x + 8, p.y + 3);
        });
    }

    /* ══════════════════════════════════════════════════════════════
     *  Sweeping radar line
     * ══════════════════════════════════════════════════════════════ */

    drawSweepLine(ctx, dt) {
        this.sweepAngle += this.sweepSpeed * dt;
        if (this.sweepAngle > Math.PI * 2) this.sweepAngle -= Math.PI * 2;

        const len = this.rangeNm * this.scale;
        const endX = this.cx + Math.cos(this.sweepAngle) * len;
        const endY = this.cy + Math.sin(this.sweepAngle) * len;

        // Leading edge
        const lineGrad = ctx.createLinearGradient(this.cx, this.cy, endX, endY);
        lineGrad.addColorStop(0, 'rgba(0, 255, 65, 0.0)');
        lineGrad.addColorStop(0.2, 'rgba(0, 255, 65, 0.4)');
        lineGrad.addColorStop(1, 'rgba(0, 255, 65, 0.7)');

        ctx.strokeStyle = lineGrad;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(this.cx, this.cy);
        ctx.lineTo(endX, endY);
        ctx.stroke();

        // Trailing glow wedge
        const trailAngle = 0.4;
        const trailGrad = ctx.createConicGradient(
            this.sweepAngle - trailAngle, this.cx, this.cy);
        trailGrad.addColorStop(0, 'rgba(0, 255, 65, 0.0)');
        trailGrad.addColorStop(0.7, 'rgba(0, 255, 65, 0.03)');
        trailGrad.addColorStop(1, 'rgba(0, 255, 65, 0.10)');

        ctx.fillStyle = trailGrad;
        ctx.beginPath();
        ctx.moveTo(this.cx, this.cy);
        ctx.arc(this.cx, this.cy, len,
                this.sweepAngle - trailAngle, this.sweepAngle);
        ctx.closePath();
        ctx.fill();
    }

    /* ══════════════════════════════════════════════════════════════
     *  History trails
     * ══════════════════════════════════════════════════════════════ */

    drawHistoryTrails(ctx) {
        this.aircraft.forEach(ac => {
            if (!ac.hist || ac.hist.length < 2) return;

            const isConflict = this.conflicts.some(
                c => c.a === ac.cs || c.b === ac.cs);

            ac.hist.forEach((pt, i) => {
                const p = this.latLonToScreen(pt[0], pt[1]);
                const alpha = 0.12 + (i / ac.hist.length) * 0.35;
                const size = 1.5 + (i / ac.hist.length) * 1;

                ctx.beginPath();
                ctx.arc(p.x, p.y, size, 0, Math.PI * 2);
                ctx.fillStyle = isConflict
                    ? `rgba(255, 61, 61, ${alpha})`
                    : `rgba(0, 255, 65, ${alpha})`;
                ctx.fill();
            });
        });
    }

    /* ══════════════════════════════════════════════════════════════
     *  Aircraft blips + data blocks
     * ══════════════════════════════════════════════════════════════ */

    drawAircraft(ctx) {
        this.aircraft.forEach(ac => {
            const p = this.latLonToScreen(ac.lat, ac.lon);

            // Skip off-screen
            if (p.x < -50 || p.x > this.width + 50 ||
                p.y < -50 || p.y > this.height + 50) return;

            const selected = (ac.cs === this.selectedCallsign);
            const hovered  = (ac.cs === this.hoveredCallsign);
            const isConflict = this.conflicts.some(
                c => c.a === ac.cs || c.b === ac.cs);

            const blipColor = isConflict ? '#ff3d3d'
                            : selected   ? '#00e5ff'
                            : hovered    ? '#88ffaa'
                            :              '#00ff41';

            /* ── Glow ────────────────────────────────────────────── */
            const glowSize = (selected || hovered) ? 12 : 8;
            ctx.beginPath();
            ctx.arc(p.x, p.y, glowSize, 0, Math.PI * 2);
            ctx.fillStyle = isConflict
                ? 'rgba(255, 61, 61, 0.12)'
                : selected ? 'rgba(0, 229, 255, 0.15)'
                : hovered  ? 'rgba(100, 255, 150, 0.12)'
                : 'rgba(0, 255, 65, 0.08)';
            ctx.fill();

            /* ── Blip dot ────────────────────────────────────────── */
            ctx.beginPath();
            ctx.arc(p.x, p.y, selected ? 4 : 3.5, 0, Math.PI * 2);
            ctx.fillStyle = blipColor;
            ctx.fill();

            /* ── Selection ring ──────────────────────────────────── */
            if (selected) {
                ctx.strokeStyle = 'rgba(0, 229, 255, 0.6)';
                ctx.lineWidth = 1.5;
                ctx.beginPath();
                ctx.arc(p.x, p.y, 14, 0, Math.PI * 2);
                ctx.stroke();

                // Rotating dashes for selected
                ctx.save();
                ctx.translate(p.x, p.y);
                ctx.rotate(Date.now() / 2000);
                ctx.setLineDash([6, 8]);
                ctx.strokeStyle = 'rgba(0, 229, 255, 0.25)';
                ctx.beginPath();
                ctx.arc(0, 0, 20, 0, Math.PI * 2);
                ctx.stroke();
                ctx.setLineDash([]);
                ctx.restore();
            }

            /* ── Hover ring ──────────────────────────────────────── */
            if (hovered && !selected) {
                ctx.strokeStyle = 'rgba(100, 255, 150, 0.35)';
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.arc(p.x, p.y, 14, 0, Math.PI * 2);
                ctx.stroke();
            }

            /* ── Current heading line ────────────────────────────── */
            const hdgRad = (ac.hdg - 90) * Math.PI / 180;
            const predLen = (ac.spd / 3600) * 60 * this.scale;
            ctx.strokeStyle = selected
                ? 'rgba(0, 229, 255, 0.35)'
                : 'rgba(0, 255, 65, 0.15)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(p.x, p.y);
            ctx.lineTo(p.x + Math.cos(hdgRad) * predLen,
                       p.y + Math.sin(hdgRad) * predLen);
            ctx.stroke();

            /* ── Target heading line (dashed, when different) ────── */
            if (selected && Math.abs(ac.hdg - ac.thdg) > 1) {
                const thdgRad = (ac.thdg - 90) * Math.PI / 180;
                ctx.strokeStyle = 'rgba(0, 229, 255, 0.2)';
                ctx.lineWidth = 1;
                ctx.setLineDash([4, 4]);
                ctx.beginPath();
                ctx.moveTo(p.x, p.y);
                ctx.lineTo(p.x + Math.cos(thdgRad) * predLen * 0.7,
                           p.y + Math.sin(thdgRad) * predLen * 0.7);
                ctx.stroke();
                ctx.setLineDash([]);
            }

            /* ── Data block ──────────────────────────────────────── */
            const dbX = p.x + 16;
            const dbY = p.y - 10;

            // Leader line
            ctx.strokeStyle = selected
                ? 'rgba(0, 229, 255, 0.25)'
                : 'rgba(0, 255, 65, 0.18)';
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(p.x + 4, p.y - 2);
            ctx.lineTo(dbX - 2, dbY + 5);
            ctx.stroke();

            // Background for readability
            if (selected || hovered) {
                ctx.fillStyle = 'rgba(5, 12, 5, 0.7)';
                ctx.fillRect(dbX - 3, dbY - 12, 90, 28);
            }

            ctx.font = '11px "JetBrains Mono", monospace';
            const altHundreds = Math.round(ac.alt / 100);
            const spdText = Math.round(ac.spd);

            // Altitude trend arrow
            let trend = '';
            if (ac.talt !== undefined) {
                if (ac.talt > ac.alt + 50) trend = '↑';
                else if (ac.talt < ac.alt - 50) trend = '↓';
                else trend = '-';
            }

            // Callsign
            ctx.fillStyle = isConflict ? '#ff3d3d'
                          : selected   ? '#00e5ff'
                          : hovered    ? '#88ffaa'
                          :              '#00ff41';
            ctx.fillText(ac.cs, dbX, dbY);

            // Alt + trend + speed
            ctx.fillStyle = selected ? 'rgba(0, 229, 255, 0.65)'
                          : 'rgba(0, 255, 65, 0.5)';
            ctx.fillText(`${altHundreds}${trend} ${spdText}`, dbX, dbY + 13);
        });
    }

    /* ══════════════════════════════════════════════════════════════
     *  Conflict highlights
     * ══════════════════════════════════════════════════════════════ */

    drawConflictHighlights(ctx) {
        const now = Date.now() / 1000;
        const pulse = 0.5 + 0.5 * Math.sin(now * 4);

        this.conflicts.forEach(c => {
            const acA = this.aircraft.find(a => a.cs === c.a);
            const acB = this.aircraft.find(a => a.cs === c.b);
            if (!acA || !acB) return;

            const pA = this.latLonToScreen(acA.lat, acA.lon);
            const pB = this.latLonToScreen(acB.lat, acB.lon);

            // Dashed warning line
            ctx.strokeStyle = `rgba(255, 61, 61, ${0.25 + pulse * 0.35})`;
            ctx.lineWidth = 1.5;
            ctx.setLineDash([5, 5]);
            ctx.beginPath();
            ctx.moveTo(pA.x, pA.y);
            ctx.lineTo(pB.x, pB.y);
            ctx.stroke();
            ctx.setLineDash([]);

            // Midpoint info
            const mx = (pA.x + pB.x) / 2;
            const my = (pA.y + pB.y) / 2;

            ctx.fillStyle = `rgba(255, 61, 61, ${0.4 + pulse * 0.4})`;
            ctx.font = '9px "JetBrains Mono", monospace';
            const latText = c.lat !== undefined ? c.lat.toFixed(1) : '?';
            const vertText = c.vert !== undefined ? Math.round(c.vert) : '?';
            ctx.fillText(`${latText}nm/${vertText}ft`, mx + 5, my - 5);
        });
    }

    /* ══════════════════════════════════════════════════════════════
     *  Click-to-select
     * ══════════════════════════════════════════════════════════════ */

    handleClick(screenX, screenY) {
        const HIT_RADIUS = 20;
        let closest = null;
        let closestDist = Infinity;

        this.aircraft.forEach(ac => {
            const p = this.latLonToScreen(ac.lat, ac.lon);
            const dist = Math.hypot(screenX - p.x, screenY - p.y);
            if (dist < HIT_RADIUS && dist < closestDist) {
                closest = ac.cs;
                closestDist = dist;
            }
        });

        this.selectedCallsign = closest;
        return closest;
    }
}
