/**
 * network.js – WebSocket Client for ATC Simulator
 *
 * Manages the WebSocket connection to the C++ backend server.
 * - Auto-reconnects on disconnect with exponential back-off.
 * - Parses incoming JSON telemetry frames.
 * - Sends JSON-encoded ATC commands to the server.
 */

class NetworkManager {
    /**
     * @param {function} onTelemetry  – callback(data) for each telemetry frame
     * @param {function} onStatus     – callback(status) for connection state changes
     */
    constructor(onTelemetry, onStatus) {
        this.ws = null;
        this.onTelemetry = onTelemetry;
        this.onStatus = onStatus;
        this.connected = false;
        this.reconnectDelay = 1000;       // ms, doubles on each failure
        this.reconnectTimer = null;
        this.url = null;
    }

    /**
     * Open the WebSocket connection.
     * @param {string} url – e.g. "ws://localhost:8080"
     */
    connect(url) {
        this.url = url;
        this._clearReconnect();

        try {
            this.ws = new WebSocket(url);
        } catch (e) {
            console.error('[Network] WebSocket creation failed:', e);
            this.onStatus('error');
            this._scheduleReconnect();
            return;
        }

        this.ws.onopen = () => {
            console.log('[Network] Connected to', url);
            this.connected = true;
            this.reconnectDelay = 1000;   // reset back-off
            this.onStatus('connected');
        };

        this.ws.onclose = () => {
            console.warn('[Network] Connection closed');
            this.connected = false;
            this.onStatus('disconnected');
            this._scheduleReconnect();
        };

        this.ws.onerror = (e) => {
            console.error('[Network] Error:', e);
            this.onStatus('error');
        };

        this.ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                if (data.type === 'telemetry') {
                    this.onTelemetry(data);
                }
            } catch (e) {
                console.error('[Network] Bad JSON:', e);
            }
        };
    }

    /**
     * Send an ATC command to the backend.
     * @param {string} callsign – e.g. "AAL123"
     * @param {string} action   – "heading" | "altitude" | "speed"
     * @param {number} value    – target value
     */
    sendCommand(callsign, action, value) {
        if (!this.connected || !this.ws) return false;
        const msg = JSON.stringify({
            type: 'command',
            callsign: callsign,
            action: action,
            value: value
        });
        this.ws.send(msg);
        console.log('[Network] Sent command:', msg);
        return true;
    }

    /** Schedule a reconnection attempt with exponential back-off. */
    _scheduleReconnect() {
        if (this.reconnectTimer) return;
        console.log(`[Network] Reconnecting in ${this.reconnectDelay}ms...`);
        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;
            this.connect(this.url);
        }, this.reconnectDelay);
        // Cap at 10 seconds
        this.reconnectDelay = Math.min(this.reconnectDelay * 2, 10000);
    }

    _clearReconnect() {
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
    }
}
