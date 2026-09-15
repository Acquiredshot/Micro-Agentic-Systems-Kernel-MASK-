import { CartesianGrid, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis, Legend } from 'recharts';
import type { HistoryPoint } from '../types';
import { categorical, ink, surface } from '../lib/theme';

function formatTime(ts: number): string {
  const d = new Date(ts);
  return d.toLocaleTimeString([], { hour12: false });
}

function ChartTooltip({ active, payload, label }: any) {
  if (!active || !payload?.length) return null;
  return (
    <div className="rounded-md border border-white/10 bg-[#1a1a19] px-3 py-2 text-xs shadow-lg">
      <div className="mb-1 text-[#898781]">{formatTime(label)}</div>
      {payload.map((p: any) => (
        <div key={p.dataKey} className="flex items-center gap-2 text-[#c3c2b7]">
          <span className="inline-block h-2 w-2 rounded-full" style={{ backgroundColor: p.color }} />
          <span>{p.name}</span>
          <span className="ml-auto tabular-nums text-white">{p.value.toFixed(2)}</span>
        </div>
      ))}
    </div>
  );
}

export function LoadChart({ data }: { data: HistoryPoint[] }) {
  return (
    <div className="rounded-lg border border-white/10 bg-[#1a1a19] p-4">
      <div className="mb-2 text-sm font-medium text-white">Load average</div>
      <ResponsiveContainer width="100%" height={220}>
        <LineChart data={data} margin={{ top: 4, right: 8, left: -16, bottom: 0 }}>
          <CartesianGrid stroke={ink.gridline} vertical={false} />
          <XAxis
            dataKey="ts"
            tickFormatter={formatTime}
            stroke={ink.muted}
            tick={{ fill: ink.muted, fontSize: 11 }}
            minTickGap={40}
          />
          <YAxis stroke={ink.muted} tick={{ fill: ink.muted, fontSize: 11 }} width={40} />
          <Tooltip content={<ChartTooltip />} cursor={{ stroke: ink.baseline }} />
          <Legend
            wrapperStyle={{ fontSize: 12, color: ink.secondary }}
            formatter={(value) => <span style={{ color: ink.secondary }}>{value}</span>}
          />
          <Line type="monotone" dataKey="load1" name="1 min" stroke={categorical.blue} strokeWidth={2} dot={false} activeDot={{ r: 4, stroke: surface.chart, strokeWidth: 2 }} isAnimationActive={false} />
          <Line type="monotone" dataKey="load5" name="5 min" stroke={categorical.orange} strokeWidth={2} dot={false} activeDot={{ r: 4, stroke: surface.chart, strokeWidth: 2 }} isAnimationActive={false} />
          <Line type="monotone" dataKey="load15" name="15 min" stroke={categorical.aqua} strokeWidth={2} dot={false} activeDot={{ r: 4, stroke: surface.chart, strokeWidth: 2 }} isAnimationActive={false} />
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}
