import { useState, useEffect } from "preact/hooks";
import { useTheme } from "../context/ThemeContext";

export function ThemeSelector() {
  const { themeData, applyTheme } = useTheme();
  const [selectedTheme, setSelectedTheme] = useState("");
  const [themes, setThemes] = useState([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);

  // Load themes on mount
  useEffect(() => {
    async function loadThemes() {
      try {
        setLoading(true);
        setError(null);
        const response = await fetch("/api/themes");
        if (!response.ok) {
          throw new Error(`HTTP error! Status: ${response.status}`);
        }
        const data = await response.json();
        setThemes(data.themes || []);
      } catch (err) {
        setError(err instanceof Error ? err.message : "Failed to fetch themes list");
      } finally {
        setLoading(false);
      }
    }

    loadThemes();
  }, []);

  // Set initial selection based on current theme
  useEffect(() => {
    if (themeData?.themeName) {
      setSelectedTheme(themeData.themeName);
    }
  }, [themeData]);

  const handleThemeSelect = async (theme) => {
    if (loading) return;

    setSelectedTheme(theme);
    setLoading(true);

    try {
      await applyTheme(theme);
    } catch (error) {
      console.error("Error applying theme:", error);
    } finally {
      setLoading(false);
    }
  };

  if (loading) {
    return <div class='text-gray-400'>Loading themes...</div>;
  }

  if (error) {
    return <div class='text-red-400'>{error}</div>;
  }

  return (
    <div class='space-y-4'>
      <div class='grid gap-2 sm:grid-cols-2 md:grid-cols-3'>
        {themes.map((theme) => {
          const isSelected = theme === selectedTheme;
          const displayName = theme.replace("THEME_", "").replace(/_/g, " ");

          return (
            <button
              key={theme}
              onClick={() => handleThemeSelect(theme)}
              disabled={loading}
              class={`
                rounded-md p-3 text-left transition-all
                ${
                  isSelected
                    ? "bg-gray-700 text-gray-300 border-2 border-primary-color"
                    : "bg-gray-700 text-gray-300 hover:bg-gray-600 border-2 border-transparent"
                }
                ${loading ? "opacity-50 cursor-not-allowed" : "cursor-pointer"}
              `}
            >
              {displayName}
            </button>
          );
        })}
      </div>
    </div>
  );
}
