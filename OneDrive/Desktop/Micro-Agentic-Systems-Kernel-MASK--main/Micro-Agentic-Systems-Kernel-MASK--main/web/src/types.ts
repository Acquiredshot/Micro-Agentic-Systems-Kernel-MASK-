export interface MaskEntry {
  ts: number;
  role: string;
  text: string;
}

export interface MaskTool {
  name: string;
  description: string;
}

export interface HistoryPoint {
  ts: number;
  load1: number;
  load5: number;
  load15: number;
  memUsedPct: number;
}

export interface MaskConfig {
  tick_interval_ms: number;
  llm_every_n_ticks: number;
  paused: boolean;
  llm_endpoint: string;
  llm_model: string;
}

export interface SnapshotMessage {
  type: 'snapshot';
  connected: boolean;
  error?: string;
  tick?: number;
  llm_busy?: boolean;
  config?: MaskConfig;
  entries?: MaskEntry[];
  tools?: MaskTool[];
  history: HistoryPoint[];
}

export interface SetConfigResult {
  type: 'set_config_result';
  ok: boolean;
  config?: MaskConfig;
  error?: string;
}

export type BridgeMessage = SnapshotMessage;
