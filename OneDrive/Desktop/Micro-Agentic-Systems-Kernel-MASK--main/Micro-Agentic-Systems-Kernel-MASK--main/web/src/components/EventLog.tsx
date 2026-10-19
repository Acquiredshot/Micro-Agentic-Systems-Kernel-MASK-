import { useEffect, useRef, useState } from 'react';
import type { MaskEntry } from '../types';
import { colorForRole, ink } from '../lib/theme';

function formatTime(ts: number): string {
  return new Date(ts).toLocaleTimeString([], { hour12: false });
}

export function EventLog({ entries }: { entries: MaskEntry[] }) {
  const scrollRef = useRef<HTMLDivElement>(null);
  const [autoScroll, setAutoScroll] = useState(true);

  useEffect(() => {
    const el = scrollRef.current;
    if (autoScroll && el) {
      el.scrollTop = el.scrollHeight;
    }
  }, [entries, autoScroll]);

  return (
    <div className="flex h-full flex-col rounded-lg border border-white/10 bg-[#1a1a19] p-4">
      <div className="mb-2 flex items-center justify-between">
        <div className="text-sm font-medium text-white">
          Event log <span style={{ color: ink.muted }}>({entries.length})</span>
        </div>
        <label className="flex items-center gap-2 text-xs" style={{ color: ink.secondary }}>
          <input
            type="checkbox"
            checked={autoScroll}
            onChange={(e) => setAutoScroll(e.target.checked)}
          />
          Auto-scroll
        </label>
      </div>

      <div ref={scrollRef} className="flex-1 overflow-y-auto">
        <table className="w-full table-fixed border-collapse text-xs">
          <thead className="sticky top-0 bg-[#1a1a19]">
            <tr style={{ color: ink.muted }}>
              <th className="w-20 py-1 text-left font-normal">Time</th>
              <th className="w-24 py-1 text-left font-normal">Role</th>
              <th className="py-1 text-left font-normal">Text</th>
            </tr>
          </thead>
          <tbody>
            {entries.map((e, i) => (
              <tr key={`${e.ts}-${i}`} className="border-t" style={{ borderColor: ink.gridline }}>
                <td className="tabular-nums py-1 pr-2 align-top" style={{ color: ink.muted }}>
                  {formatTime(e.ts)}
                </td>
                <td className="py-1 pr-2 align-top font-mono" style={{ color: colorForRole(e.role) }}>
                  {e.role}
                </td>
                <td
                  className="truncate py-1 align-top"
                  style={{ color: ink.secondary }}
                  title={e.text}
                >
                  {e.text}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
