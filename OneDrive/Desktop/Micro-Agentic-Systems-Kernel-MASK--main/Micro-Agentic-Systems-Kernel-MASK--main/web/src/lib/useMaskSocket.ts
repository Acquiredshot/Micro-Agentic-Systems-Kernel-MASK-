import { useCallback, useEffect, useRef, useState } from 'react';
import type { BridgeMessage, MaskConfig, SetConfigResult } from '../types';

const BRIDGE_URL = import.meta.env.VITE_BRIDGE_URL || 'ws://localhost:8765';

export interface MaskSocketState {
  bridgeConnected: boolean;
  daemonConnected: boolean;
  daemonError: string | null;
  data: BridgeMessage | null;
  lastConfigResult: SetConfigResult | null;
  setConfig: (partial: Partial<MaskConfig>) => void;
}

// Owns the WebSocket connection to the Node bridge (see bridge/server.js),
// reconnecting automatically if the bridge itself restarts. The bridge's
// own polling of maskd is reflected in `connected`/`error` on each message,
// so bridge-down and daemon-down are distinguishable in the UI. Also
// exposes setConfig() for the control panel to push live config changes
// through the bridge to the daemon.
export function useMaskSocket(): MaskSocketState {
  const [state, setState] = useState<Omit<MaskSocketState, 'setConfig'>>({
    bridgeConnected: false,
    daemonConnected: false,
    daemonError: null,
    data: null,
    lastConfigResult: null,
  });
  const wsRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    let cancelled = false;
    let retryTimer: ReturnType<typeof setTimeout>;

    function connect() {
      if (cancelled) return;
      const ws = new WebSocket(BRIDGE_URL);
      wsRef.current = ws;

      ws.onopen = () => {
        setState((s) => ({ ...s, bridgeConnected: true }));
      };

      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);
          if (msg.type === 'snapshot') {
            setState((s) => ({
              ...s,
              bridgeConnected: true,
              daemonConnected: msg.connected,
              daemonError: msg.connected ? null : msg.error ?? 'unknown error',
              data: msg,
            }));
          } else if (msg.type === 'set_config_result') {
            setState((s) => ({ ...s, lastConfigResult: msg }));
          }
        } catch {
          // ignore malformed frames
        }
      };

      ws.onclose = () => {
        if (cancelled) return;
        setState((s) => ({ ...s, bridgeConnected: false, daemonConnected: false }));
        retryTimer = setTimeout(connect, 1000);
      };

      ws.onerror = () => {
        ws.close();
      };
    }

    connect();
    return () => {
      cancelled = true;
      clearTimeout(retryTimer);
      wsRef.current?.close();
    };
  }, []);

  const setConfig = useCallback((partial: Partial<MaskConfig>) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify({ type: 'set_config', config: partial }));
    }
  }, []);

  return { ...state, setConfig };
}
