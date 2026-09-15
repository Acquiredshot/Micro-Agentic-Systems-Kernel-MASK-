import { Area, AreaChart, CartesianGrid, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import type { HistoryPoint } from '../types';
import { categorical, ink, surface } from '../lib/theme';

function formatTime(ts: number): string {
  return new Date(ts).toLocaleTimeString([], { hour12: false });
}

function ChartTooltip({ active, payload, label }: any) {
  if (!active || !payload?.length) return null;
  return (
    <div className="rounded-md border border-white/10 bg-[#1a1a19] px-3 py-2 text-xs shadow-lg">
      <div className="mb-1 text-[#898781]">{formatTime(label)}</div>
      <div className="text-[#c3c2b7]">
        Memory used <span className="ml-2 tabular-nums text-white">{payload[0].value.toFixed(1)}%</span>
      </div>
    </div>
  );
}

// Single series: no legend needed (dataviz skill -- the title already
// says what's plotted). Area fill at ~10% opacity, a wash rather than a
// saturated block.
export function MemoryChart({ data }: { data: HistoryPoint[] }) {
  return (
    <div className="rounded-lg border border-white/10 bg-[#1a1a19] p-4">
      <div className="mb-2 text-sm font-medium text-white">Memory used %</div>
      <ResponsiveContainer width="100%" height={220}>
        <AreaChart data={data} margin={{ top: 4, right: 8, left: -16, bottom: 0 }}>
          <defs>
            <linearGradient id="memFill" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor={categorical.blue} stopOpacity={0.25} />
              <stop offset="100%" stopColor={categorical.blue} stopOpacity={0.02} />
            </linearGradient>
          </defs>
          <CartesianGrid stroke={ink.gridline} vertical={false} />
          <XAxis
            dataKey="ts"
            tickFormatter={formatTime}
            stroke={ink.muted}
            tick={{ fill: ink.muted, fontSize: 11 }}
            minTickGap={40}
          />
          <YAxis stroke={ink.muted} tick={{ fill: ink.muted, fontSize: 11 }} width={40} domain={[0, 100]} />
          <Tooltip content={<ChartTooltip />} cursor={{ stroke: ink.baseline }} />
          <Area
            type="monotone"
            dataKey="memUsedPct"
            stroke={categorical.blue}
            strokeWidth={2}
            fill="url(#memFill)"
            dot={false}
            activeDot={{ r: 4, stroke: surface.chart, strokeWidth: 2 }}
            isAnimationActive={false}
          />
        </AreaChart>
      </ResponsiveContainer>
    </div>
  );
}
