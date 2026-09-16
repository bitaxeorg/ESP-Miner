/**
 * The available surface schemes.
 *
 * Single source of truth. The scheme id becomes a `theme-<id>` class on the
 * document element and the matching block in
 * `layout/styles/layout/_theme.scss` supplies the tokens. Adding a scheme
 * previously meant editing the radio buttons, the class list that
 * LayoutService clears before applying the new one, and the stylesheet — and
 * missing the second of those left the old scheme's class in place alongside
 * the new one.
 */
export interface ColorScheme {
  /** Matches the `theme-<id>` class and the value persisted to NVS. */
  id: string;
  label: string;
  /**
   * Whether the scheme paints a dark surface. Drives the `dark-mode` class,
   * which several components key off independently of the theme tokens.
   */
  dark: boolean;
  /** Shown under the label so the difference between schemes is legible. */
  description: string;
}

export const COLOR_SCHEMES: ColorScheme[] = [
  {
    id: 'midnight',
    label: 'Midnight',
    dark: true,
    description: 'True black, for OLED panels and dark rooms',
  },
  {
    id: 'dark',
    label: 'Dark',
    dark: true,
    description: 'The default deep navy',
  },
  {
    id: 'slate',
    label: 'Slate',
    dark: true,
    description: 'Softer neutral grey, lower contrast',
  },
  {
    id: 'light',
    label: 'Light',
    dark: true,
    description: 'Lifted navy',
  },
  {
    id: 'white',
    label: 'White',
    dark: false,
    description: 'Light surface for bright rooms',
  },
];

/** Every `theme-*` class the layout may have applied, for removal. */
export const COLOR_SCHEME_CLASSES: string[] = COLOR_SCHEMES.map(s => `theme-${s.id}`);

export const DEFAULT_COLOR_SCHEME = 'dark';

export function isDarkScheme(id: string): boolean {
  return COLOR_SCHEMES.find(s => s.id === id)?.dark ?? true;
}
