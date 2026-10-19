import type { MaskTool } from '../types';
import { status } from '../lib/theme';

export function ToolsPanel({ tools }: { tools: MaskTool[] }) {
  return (
    <div className="rounded-lg border border-white/10 bg-[#1a1a19] p-4">
      <div className="mb-3 text-sm font-medium text-white">Registered tools</div>
      {tools.length === 0 && <div className="text-sm text-[#898781]">No tools registered</div>}
      <div className="flex flex-col gap-3">
        {tools.map((t) => (
          <div key={t.name}>
            <div className="font-mono text-sm" style={{ color: status.good }}>{t.name}</div>
            <div className="text-xs text-[#c3c2b7]">{t.description}</div>
          </div>
        ))}
      </div>
    </div>
  );
}
