// Dark-mode values from the project's validated reference palette
// (see the dataviz skill's references/palette.md). This dashboard is
// dark-only by design, so only the dark column is used.

export const surface = {
  chart: '#1a1a19',
  page: '#0d0d0d',
} as const;

export const ink = {
  primary: '#ffffff',
  secondary: '#c3c2b7',
  muted: '#898781',
  gridline: '#2c2c2a',
  baseline: '#383835',
  border: 'rgba(255,255,255,0.10)',
} as const;

// Fixed-order categorical slots (first three validate all-pairs CVD-safe
// in dark mode) -- always assign to series in this order, never cycled.
export const categorical = {
  blue: '#3987e5',
  orange: '#d95926',
  aqua: '#199e70',
} as const;

export const status = {
  good: '#0ca30c',
  warning: '#fab219',
  serious: '#ec835a',
  critical: '#e66767',
} as const;

// Role colors for ring-buffer entry roles in the event log, distinct from
// the categorical chart slots so they never impersonate a data series.
export const roleColor: Record<string, string> = {
  system: ink.muted,
  sysinfo: '#5598e7',
  llm: '#9085e9',
  tool: status.good,
  llm_error: status.critical,
};

export function colorForRole(role: string): string {
  return roleColor[role] ?? ink.secondary;
}
