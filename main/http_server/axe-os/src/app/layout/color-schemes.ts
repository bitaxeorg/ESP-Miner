/** Surface schemes. The id becomes a `theme-<id>` class and a block in _theme.scss. */
export interface ColorScheme {
  id: string;
  label: string;
  /** Drives the `dark-mode` class. */
  dark: boolean;
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

export const COLOR_SCHEME_CLASSES: string[] = COLOR_SCHEMES.map(s => `theme-${s.id}`);

export const DEFAULT_COLOR_SCHEME = 'dark';

export function isDarkScheme(id: string): boolean {
  return COLOR_SCHEMES.find(s => s.id === id)?.dark ?? true;
}
