import { createContext } from "preact";
import { useState, useEffect, useContext } from "preact/hooks";
import { ComponentChildren } from "preact";

interface ThemeData {
  colorScheme: string;
  theme: string;
  accentColors: {
    [key: string]: string;
  };
}

interface ThemeContextType {
  themeData: ThemeData | null;
  loading: boolean;
  error: string | null;
  fetchTheme: () => Promise<void>;
}

const defaultThemeContext: ThemeContextType = {
  themeData: null,
  loading: false,
  error: null,
  fetchTheme: async () => {},
};

export const ThemeContext = createContext<ThemeContextType>(defaultThemeContext);

export function ThemeProvider({ children }: { children: ComponentChildren }) {
  const [themeData, setThemeData] = useState<ThemeData | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const fetchTheme = async () => {
    try {
      setLoading(true);
      setError(null);

      const response = await fetch("/api/theme");

      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }

      const data = await response.json();
      setThemeData(data);

      // Apply theme colors to CSS variables
      if (data && data.accentColors) {
        Object.entries(data.accentColors).forEach(([key, value]) => {
          document.documentElement.style.setProperty(key, value as string);
        });

        // Apply color scheme
        if (data.colorScheme) {
          document.documentElement.setAttribute("data-color-scheme", data.colorScheme);
        }
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to fetch theme data");
    } finally {
      setLoading(false);
    }
  };

  // Fetch theme on mount
  useEffect(() => {
    fetchTheme();
  }, []);

  return (
    <ThemeContext.Provider value={{ themeData, loading, error, fetchTheme }}>
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
