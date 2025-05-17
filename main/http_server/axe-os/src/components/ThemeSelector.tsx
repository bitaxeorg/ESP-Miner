import { useState, useEffect } from "preact/hooks";
import { useTheme } from "../context/ThemeContext";

const themeOptions = [
  "THEME_ACS_DEFAULT",
  "THEME_BITAXE_RED",
  "THEME_BLOCKSTREAM_JADE",
  "THEME_BLOCKSTREAM_BLUE",
  "THEME_SOLO_SATOSHI",
  "THEME_SOLO_MINING_CO",
  "THEME_BTCMAGAZINE",
  "THEME_VOSKCOIN",
];

export function ThemeSelector() {
  const { themeData, applyTheme } = useTheme();
  const [selectedTheme, setSelectedTheme] = useState("");
  const [response, setResponse] = useState(null);
  const [loading, setLoading] = useState(false);

  // Set initial selection based on current theme
  useEffect(() => {
    if (themeData?.themeName) {
      setSelectedTheme(themeData.themeName);
    }
  }, [themeData]);

  const handleThemeChange = async (e) => {
    const newTheme = e.target.value;
    setSelectedTheme(newTheme);
    setLoading(true);

    try {
      // Direct call to applyTheme with the theme name
      await applyTheme(newTheme);

      // Fetch the new theme details to display in pre tag
      const themeResponse = await fetch("/api/theme");
      if (themeResponse.ok) {
        const data = await themeResponse.json();
        setResponse(data);
      }
    } catch (error) {
      console.error("Error applying theme:", error);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div class='theme-selector'>
      <div class='mb-4'>
        <label class='block text-sm font-medium text-gray-200 mb-2'>Select Theme</label>
        <select
          value={selectedTheme}
          onChange={handleThemeChange}
          disabled={loading}
          class='w-full bg-gray-800 border border-gray-700 rounded-md py-2 px-3 text-gray-300'
        >
          <option value='' disabled>
            Select a theme
          </option>
          {themeOptions.map((theme) => (
            <option key={theme} value={theme}>
              {theme.replace("THEME_", "").replace(/_/g, " ")}
            </option>
          ))}
        </select>
      </div>

      {loading && <div class='text-gray-300 text-sm'>Applying theme...</div>}

      {response && (
        <div class='mt-4'>
          <h4 class='text-sm font-medium text-gray-400 mb-2'>Applied Theme</h4>
          <pre class='bg-gray-700 p-3 rounded-md overflow-auto max-h-48 text-xs text-gray-300'>
            {JSON.stringify(response, null, 2)}
          </pre>
        </div>
      )}
    </div>
  );
}
