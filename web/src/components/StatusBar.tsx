import { status, ink } from '../lib/theme';

interface StatusDotProps {
  color: string;
  label: string;
}

function StatusDot({ color, label }: StatusDotProps) {
  return (
    <div className="flex items-center gap-2">
      <span className="inline-block h-2.5 w-2.5 rounded-full" style={{ backgroundColor: color }} />
      <span className="text-sm" style={{ color }}>{label}</span>
    </div>
  );
}

interface StatusBarProps {
  bridgeConnected: boolean;
  daemonConnected: boolean;
  daemonError: string | null;
  llmBusy: boolean;
}

export function StatusBar({ bridgeConnected, daemonConnected, daemonError, llmBusy }: StatusBarProps) {
  return (
    <header className="flex items-center justify-between border-b border-white/10 px-6 py-4">
      <div className="flex items-baseline gap-3">
        <h1 className="font-mono text-lg font-semibold text-[#3987e5]">MASK</h1>
        <span className="text-sm text-[#898781]">Micro Agentic Systems Kernel</span>
      </div>

      <div className="flex items-center gap-6">
        {!bridgeConnected && <StatusDot color={status.critical} label="bridge unreachable" />}
        {bridgeConnected && !daemonConnected && (
          <StatusDot color={status.critical} label={`daemon unreachable${daemonError ? `: ${daemonError}` : ''}`} />
        )}
        {bridgeConnected && daemonConnected && <StatusDot color={status.good} label="connected" />}

        {bridgeConnected && daemonConnected && (
          llmBusy
            ? <StatusDot color={status.warning} label="reasoning" />
            : <StatusDot color={ink.muted} label="idle" />
        )}
      </div>
    </header>
  );
}
