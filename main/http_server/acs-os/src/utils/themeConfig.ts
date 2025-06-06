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

// Static theme assets map - no API calls
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
      primary: "#666666",
      secondary: "#888888",
      accent: "#999999",
      background: "#333333",
      text: "#cccccc",
    },
  },
  THEME_BLOCKSTREAM_JADE: {
    logo: "/jade-light-logo.png",
    favicon: "/favicon.png",
    colors: {
      primary: "#666666",
      secondary: "#888888",
      accent: "#999999",
      background: "#333333",
      text: "#cccccc",
    },
  },
  THEME_BLOCKSTREAM_BLUE: {
    logo: "/blockstream-light-logo.png",
    favicon: "/favicon.png",
    colors: {
      primary: "#666666",
      secondary: "#888888",
      accent: "#999999",
      background: "#333333",
      text: "#cccccc",
    },
  },
  THEME_SOLO_SATOSHI: {
    logo: "/solo-satoshi-logo.png",
    favicon: "/favicon.png",
    colors: {
      primary: "#666666",
      secondary: "#888888",
      accent: "#999999",
      background: "#333333",
      text: "#cccccc",
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
