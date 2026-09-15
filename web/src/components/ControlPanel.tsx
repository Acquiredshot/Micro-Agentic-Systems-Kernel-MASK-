import { useEffect, useState } from 'react';
import type { MaskConfig, SetConfigResult } from '../types';
import { status, ink } from '../lib/theme';

interface ControlPanelProps {
  config: MaskConfig | undefined;
  lastResult: SetConfigResult | null;
  onApply: (partial: Partial<MaskConfig>) => void;
  disabled: boolean;
}

function fieldsEqual(a: MaskConfig, b: MaskConfig): boolean {
  return (
    a.tick_interval_ms === b.tick_interval_ms &&
    a.llm_every_n_ticks === b.llm_every_n_ticks &&
    a.paused === b.paused &&
    a.llm_endpoint === b.llm_endpoint &&
    a.llm_model === b.llm_model
  );
}

// A local "draft" the user edits, separate from the daemon-reported config
// broadcast every ~500ms -- otherwise every keystroke would be clobbered
// by the next snapshot. The draft only re-syncs from the server value
// when the user hasn't started editing (or just applied successfully).
export function ControlPanel({ config, lastResult, onApply, disabled }: ControlPanelProps) {
  const [draft, setDraft] = useState<MaskConfig | null>(config ?? null);
  const [dirty, setDirty] = useState(false);

  useEffect(() => {
    if (config && !dirty) {
      setDraft(config);
    }
  }, [config, dirty]);

  useEffect(() => {
    if (lastResult?.ok) {
      setDirty(false);
    }
  }, [lastResult]);

  if (!draft) {
    return (
      <div className="rounded-lg border border-white/10 bg-[#1a1a19] p-4">
        <div className="text-sm font-medium text-white">Control panel</div>
        <div className="mt-2 text-xs text-[#898781]">Waiting for daemon config…</div>
      </div>
    );
  }

  function update<K extends keyof MaskConfig>(key: K, value: MaskConfig[K]) {
    setDraft((d) => (d ? { ...d, [key]: value } : d));
    setDirty(true);
  }

  function handleApply() {
    if (!draft) return;
    onApply(draft);
  }

  function handleReset() {
    if (config) setDraft(config);
    setDirty(false);
  }

  const hasChanges = config ? !fieldsEqual(draft, config) : false;

  return (
    <div className="rounded-lg border border-white/10 bg-[#1a1a19] p-4">
      <div className="mb-3 flex items-center justify-between">
        <div className="text-sm font-medium text-white">Control panel</div>
        {dirty && <span className="text-xs" style={{ color: status.warning }}>unsaved changes</span>}
      </div>

      <div className="flex flex-col gap-3 text-sm">
        <label className="flex flex-col gap-1">
          <span style={{ color: ink.secondary }}>Tick interval (ms)</span>
          <input
            type="number"
            min={100}
            max={3600000}
            value={draft.tick_interval_ms}
            onChange={(e) => update('tick_interval_ms', Number(e.target.value))}
            className="rounded border border-white/10 bg-[#0d0d0d] px-2 py-1 text-white outline-none focus:border-[#3987e5]"
          />
        </label>

        <label className="flex flex-col gap-1">
          <span style={{ color: ink.secondary }}>Reasoning cycle every N ticks</span>
          <input
            type="number"
            min={0}
            value={draft.llm_every_n_ticks}
            onChange={(e) => update('llm_every_n_ticks', Number(e.target.value))}
            className="rounded border border-white/10 bg-[#0d0d0d] px-2 py-1 text-white outline-none focus:border-[#3987e5]"
          />
        </label>

        <label className="flex items-center gap-2">
          <input
            type="checkbox"
            checked={draft.paused}
            onChange={(e) => update('paused', e.target.checked)}
          />
          <span style={{ color: ink.secondary }}>Pause reasoning</span>
        </label>

        <label className="flex flex-col gap-1">
          <span style={{ color: ink.secondary }}>LLM endpoint</span>
          <input
            type="text"
            value={draft.llm_endpoint}
            onChange={(e) => update('llm_endpoint', e.target.value)}
            className="rounded border border-white/10 bg-[#0d0d0d] px-2 py-1 font-mono text-xs text-white outline-none focus:border-[#3987e5]"
          />
        </label>

        <label className="flex flex-col gap-1">
          <span style={{ color: ink.secondary }}>LLM model</span>
          <input
            type="text"
            value={draft.llm_model}
            onChange={(e) => update('llm_model', e.target.value)}
            className="rounded border border-white/10 bg-[#0d0d0d] px-2 py-1 font-mono text-xs text-white outline-none focus:border-[#3987e5]"
          />
        </label>

        <div className="flex gap-2 pt-1">
          <button
            onClick={handleApply}
            disabled={disabled || !hasChanges}
            className="rounded bg-[#3987e5] px-3 py-1.5 text-xs font-medium text-white disabled:cursor-not-allowed disabled:opacity-40"
          >
            Apply
          </button>
          <button
            onClick={handleReset}
            disabled={!hasChanges}
            className="rounded border border-white/10 px-3 py-1.5 text-xs text-[#c3c2b7] disabled:cursor-not-allowed disabled:opacity-40"
          >
            Reset
          </button>
        </div>

        {lastResult && !lastResult.ok && (
          <div className="text-xs" style={{ color: status.critical }}>
            {lastResult.error}
          </div>
        )}
      </div>
    </div>
  );
}
