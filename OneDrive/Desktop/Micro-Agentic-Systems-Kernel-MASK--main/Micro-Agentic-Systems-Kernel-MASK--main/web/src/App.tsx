import { useMaskSocket } from './lib/useMaskSocket';
import { StatusBar } from './components/StatusBar';
import { StatTile } from './components/StatTile';
import { LoadChart } from './components/LoadChart';
import { MemoryChart } from './components/MemoryChart';
import { ToolsPanel } from './components/ToolsPanel';
import { EventLog } from './components/EventLog';
import { ControlPanel } from './components/ControlPanel';

function App() {
  const { bridgeConnected, daemonConnected, daemonError, data, lastConfigResult, setConfig } = useMaskSocket();

  const entries = data?.entries ?? [];
  const tools = data?.tools ?? [];
  const history = data?.history ?? [];
  const latest = history[history.length - 1];

  return (
    <div className="flex h-screen flex-col bg-[#0d0d0d] text-white">
      <StatusBar
        bridgeConnected={bridgeConnected}
        daemonConnected={daemonConnected}
        daemonError={daemonError}
        llmBusy={data?.llm_busy ?? false}
      />

      <div className="grid flex-1 grid-cols-4 gap-4 overflow-hidden p-6">
        <div className="col-span-1 flex flex-col gap-4 overflow-y-auto">
          <div className="grid grid-cols-2 gap-3">
            <StatTile label="Tick" value={data?.tick != null ? String(data.tick) : '—'} />
            <StatTile
              label="Load (1m)"
              value={latest ? latest.load1.toFixed(2) : '—'}
            />
            <StatTile
              label="Memory used"
              value={latest ? `${latest.memUsedPct.toFixed(1)}%` : '—'}
            />
            <StatTile
              label="History"
              value={String(history.length)}
              sub="samples"
            />
          </div>

          <ToolsPanel tools={tools} />

          <ControlPanel
            config={data?.config}
            lastResult={lastConfigResult}
            onApply={setConfig}
            disabled={!daemonConnected}
          />
        </div>

        <div className="col-span-3 flex flex-col gap-4 overflow-hidden">
          <div className="grid grid-cols-2 gap-4">
            <LoadChart data={history} />
            <MemoryChart data={history} />
          </div>
          <div className="flex-1 overflow-hidden">
            <EventLog entries={entries} />
          </div>
        </div>
      </div>
    </div>
  );
}

export default App;
