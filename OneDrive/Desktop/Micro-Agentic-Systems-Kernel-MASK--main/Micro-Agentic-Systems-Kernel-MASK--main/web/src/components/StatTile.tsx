interface StatTileProps {
  label: string;
  value: string;
  sub?: string;
}

// Stat tile contract (dataviz skill): label in sentence case, value in the
// default proportional figures (not tabular -- this is a display-size
// number, not a column that must align).
export function StatTile({ label, value, sub }: StatTileProps) {
  return (
    <div className="rounded-lg border border-white/10 bg-[#1a1a19] px-4 py-3">
      <div className="text-xs text-[#898781]">{label}</div>
      <div className="mt-1 text-2xl font-semibold text-white">{value}</div>
      {sub && <div className="mt-0.5 text-xs text-[#c3c2b7]">{sub}</div>}
    </div>
  );
}
