// Map theme names to their corresponding assets
export interface ThemeAssets {
  logo: string;
  favicon: string;
  colors: {
    primary: string;
    secondary: string;
    accent: string;
    background: string;
    text: string;
  };
}
export const themeAssetsMap: Record<string, ThemeAssets> = {
  THEME_ACS_DEFAULT: {
    logo: "/acs-brandmark-white.png",
    favicon: "/favicon.png",
    colors: {
      primary: "blue-600",
      secondary: "blue-400",
      accent: "red-500",
      background: "gray-900",
      text: "white",
    },
  },
  THEME_BITAXE_RED: {
    logo: "/bitaxe-light-logo.png",
    favicon: "/favicon.png",
    colors: {
      primary: "blue-600",
      secondary: "blue-400",
      accent: "red-500",
      background: "gray-900",
      text: "white",
    },
  },
  THEME_BLOCKSTREAM_JADE: {
    logo: "/jade-light-logo.png",
    favicon: "/favicon.png",
    colors: {
      primary: "blue-600",
      secondary: "blue-400",
      accent: "red-500",
      background: "gray-900",
      text: "white",
    },
  },
  THEME_BLOCKSTREAM_BLUE: {
    logo: "/blockstream-light-logo.png",
    favicon: "/favicon.png",
    colors: {
      primary: "blue-600",
      secondary: "blue-400",
      accent: "red-500",
      background: "gray-900",
      text: "white",
    },
  },
  THEME_SOLO_SATOSHI: {
    logo: "/solo-satoshi-logo.png",
    favicon: "/favicon.png",
    colors: {
      primary: "blue-600",
      secondary: "blue-400",
      accent: "red-500",
      background: "gray-900",
      text: "white",
    },
  },
  THEME_SOLO_MINING_CO: {
    logo: "/acs-brandmark-white.png",
    favicon: "/favicon.png",
    colors: {
      primary: "blue-600",
      secondary: "blue-400",
      accent: "red-500",
      background: "gray-900",
      text: "white",
    },
  },
  THEME_BTCMAGAZINE: {
    logo: "/btc-magazine-light-logo.png",
    favicon: "/favicon.png",
    colors: {
      primary: "blue-600",
      secondary: "blue-400",
      accent: "red-500",
      background: "gray-900",
      text: "white",
    },
  },
  THEME_VOSKCOIN: {
    logo: "/voskcoin-light-logo.png",
    favicon: "/favicon.png",
    colors: {
      primary: "blue-600",
      secondary: "blue-400",
      accent: "red-500",
      background: "gray-900",
      text: "white",
    },
  },
  THEME_AMERICANBTC: {
    logo: "/american-btc-logo.svg",
    favicon: "/favicon.png",
    colors: {
      primary: "blue-600",
      secondary: "blue-400",
      accent: "red-500",
      background: "gray-900",
      text: "white",
    },
  },
  THEME_HUT8: {
    logo: "/hut8-logo.png",
    favicon: "/favicon.png",
    colors: {
      primary: "blue-600",
      secondary: "blue-400",
      accent: "red-500",
      background: "gray-900",
      text: "white",
    },
  },
  THEME_LUXOR: {
    logo: "/luxor-light-logo.svg",
    favicon: "/favicon.png",
    colors: {
      primary: "blue-600",
      secondary: "blue-400",
      accent: "red-500",
      background: "gray-900",
      text: "white",
    },
  },
};

// Default assets to use as fallback
export const defaultAssets: ThemeAssets = {
  logo: "/acs-brandmark-white.png",
  favicon: "/favicon.png",
  colors: {
    primary: "blue-600",
    secondary: "blue-400",
    accent: "red-500",
    background: "gray-900",
    text: "white",
  },
};
