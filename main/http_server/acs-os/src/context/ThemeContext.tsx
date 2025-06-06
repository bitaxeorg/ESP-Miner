import { createContext } from "preact";
import { useState, useContext } from "preact/hooks";
import { ComponentChildren } from "preact";
import { defaultAssets } from "../utils/themeConfig";

interface ThemeData {
  themeName: string;
  primaryColor: string;
  secondaryColor: string;
  backgroundColor: string;
  textColor: string;
  borderColor: string;
}

interface ThemeContextType {
  themeData: ThemeData | null;
  loading: boolean;
  error: string | null;
  fetchTheme: () => Promise<void>;
  fetchThemes: () => Promise<void>;
  applyTheme: (themeName: string) => Promise<void>;
  getThemeLogo: () => string;
  getThemeFavicon: () => string;
}

const defaultThemeContext: ThemeContextType = {
  themeData: null,
  loading: false,
  error: null,
  fetchTheme: async () => {},
  fetchThemes: async () => {},
  applyTheme: async () => {},
  getThemeLogo: () => defaultAssets.logo,
  getThemeFavicon: () => defaultAssets.favicon,
};

export const ThemeContext = createContext<ThemeContextType>(defaultThemeContext);

export function ThemeProvider({ children }: { children: ComponentChildren }) {
  // Disabled theme state - using static values
  const [themeData] = useState<ThemeData | null>({
    themeName: "THEME_ACS_DEFAULT",
    primaryColor: "#3b82f6",
    secondaryColor: "#60a5fa",
    backgroundColor: "#111827",
    textColor: "#f9fafb",
    borderColor: "#374151",
  });
  const [loading] = useState(false);
  const [error] = useState<string | null>(null);

  // DISABLED: FETCH CURRENT THEME
  const fetchTheme = async () => {
    // Theme functionality temporarily disabled
    console.warn("Theme functionality is temporarily disabled");
  };

  // DISABLED: LIST THEMES
  const fetchThemes = async () => {
    // Theme functionality temporarily disabled
    console.warn("Theme functionality is temporarily disabled");
  };

  // DISABLED: APPLY THEME
  const applyTheme = async (themeName: string) => {
    // Theme functionality temporarily disabled
    console.warn("Theme functionality is temporarily disabled - cannot apply theme:", themeName);
  };

  // Get the default logo (static)
  const getThemeLogo = () => {
    return defaultAssets.logo;
  };

  // Get the default favicon (static)
  const getThemeFavicon = () => {
    return defaultAssets.favicon;
  };

  // DISABLED: No theme initialization on mount
  // useEffect removed - no API calls or dynamic theme loading

  return (
    <ThemeContext.Provider
      value={{
        themeData,
        loading,
        error,
        fetchTheme,
        fetchThemes,
        applyTheme,
        getThemeLogo,
        getThemeFavicon,
      }}
    >
      {children}
    </ThemeContext.Provider>
  );
}

// Custom hook to use the theme context
export function useTheme() {
  const context = useContext(ThemeContext);
  if (context === undefined) {
    throw new Error("useTheme must be used within a ThemeProvider");
  }
  return context;
}
