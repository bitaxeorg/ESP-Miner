import { fetchThemeData } from "./api";

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

// Function to convert hex color to Tailwind-like color name or return the hex value
function convertColorToTailwind(hexColor: string): string {
  // For now, return the hex color as-is since we're using CSS variables
  // In the future, this could map to Tailwind color names
  return hexColor;
}

// Function to update theme assets with API data
async function updateThemeAssetsFromAPI(themeName: string): Promise<ThemeAssets | null> {
  try {
    const themeData = await fetchThemeData(themeName);
    if (!themeData) return null;

    const currentAssets = themeAssetsMap[themeName];
    if (!currentAssets) return null;

    // Create updated assets with API colors
    const updatedAssets: ThemeAssets = {
      logo: currentAssets.logo,
      favicon: currentAssets.favicon,
      colors: {
        primary: convertColorToTailwind(themeData.primaryColor),
        secondary: convertColorToTailwind(themeData.secondaryColor),
        accent: convertColorToTailwind(themeData.borderColor), // Using borderColor as accent
        background: convertColorToTailwind(themeData.backgroundColor),
        text: convertColorToTailwind(themeData.textColor),
      },
    };

    return updatedAssets;
  } catch (error) {
    console.error(`Failed to update theme assets for ${themeName}:`, error);
    return null;
  }
}

// Function to initialize all themes with API data (except THEME_ACS_DEFAULT)
export async function initializeThemeAssets(): Promise<void> {
  const themeNames = Object.keys(themeAssetsMap);

  console.log("Initializing theme assets from API...");

  for (const themeName of themeNames) {
    // Skip THEME_ACS_DEFAULT as requested
    if (themeName === 'THEME_ACS_DEFAULT') {
      continue;
    }

    try {
      console.log(`Fetching colors for ${themeName}...`);
      const updatedAssets = await updateThemeAssetsFromAPI(themeName);
      if (updatedAssets) {
        themeAssetsMap[themeName] = updatedAssets;
        console.log(`Updated ${themeName} with colors:`, updatedAssets.colors);
      } else {
        console.warn(`Failed to get API data for ${themeName}, keeping placeholder colors`);
      }
    } catch (error) {
      console.error(`Failed to initialize theme ${themeName}:`, error);
    }
  }

  console.log("Theme assets initialization complete");
}

// Placeholder colors for themes that will be populated from API
const API_THEME_PLACEHOLDER: Omit<ThemeAssets, 'logo' | 'favicon'> = {
  colors: {
    primary: "#666666", // Placeholder - will be replaced by API
    secondary: "#888888", // Placeholder - will be replaced by API
    accent: "#999999", // Placeholder - will be replaced by API
    background: "#333333", // Placeholder - will be replaced by API
    text: "#cccccc", // Placeholder - will be replaced by API
  },
};

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
    ...API_THEME_PLACEHOLDER,
  },
  THEME_BLOCKSTREAM_JADE: {
    logo: "/jade-light-logo.png",
    favicon: "/favicon.png",
    ...API_THEME_PLACEHOLDER,
  },
  THEME_BLOCKSTREAM_BLUE: {
    logo: "/blockstream-light-logo.png",
    favicon: "/favicon.png",
    ...API_THEME_PLACEHOLDER,
  },
  THEME_SOLO_SATOSHI: {
    logo: "/solo-satoshi-logo.png",
    favicon: "/favicon.png",
    ...API_THEME_PLACEHOLDER,
  },
  // THEME_SOLO_MINING_CO: {
  //   logo: "/acs-brandmark-white.png",
  //   favicon: "/favicon.png",
  //   ...API_THEME_PLACEHOLDER,
  // },
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
