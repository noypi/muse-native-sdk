// FR-M3-005 / AC-M3-010 — theme typing surface (T8).
// Light/dark plus Dynamic Color repaint as a token property update to
// mounted descriptors (bodies not re-run per RULE-MCORE-001).

export type ThemeMode = "light" | "dark";

export interface M3Palette {
  primary?: string;
  onPrimary?: string;
  surface?: string;
  onSurface?: string;
  [role: string]: string | undefined;
}

export interface ResolvedTokens extends M3Palette {
  mode: ThemeMode;
}

export interface SetThemeInput {
  mode: ThemeMode;
  seed: string;
  dynamic?: M3Palette | null;
}

export interface SetThemeResult {
  repainted: number;
  reinstantiated: 0;
  frames: 1;
}

export declare function setTheme(input: SetThemeInput): SetThemeResult;
export declare function resolveTokens(component: string, mode: ThemeMode): ResolvedTokens;
