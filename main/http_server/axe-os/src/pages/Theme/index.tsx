import { useState } from "preact/hooks";

interface ThemeData {
  colorScheme: string;
  theme: string;
  accentColors: {
    [key: string]: string;
  };
}

export function Theme() {
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
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to fetch theme data");
    } finally {
      setLoading(false);
    }
  };

  return (
    <div class='space-y-6'>
      <div class='rounded-lg bg-gray-900 p-6'>
        <h2 class='mb-4 text-xl font-semibold text-gray-100'>Theme Configuration</h2>

        <button
          onClick={fetchTheme}
          disabled={loading}
          class='rounded-md bg-blue-600 px-4 py-2 text-white hover:bg-blue-700 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:ring-offset-2 disabled:opacity-50'
        >
          {loading ? "Loading..." : "Fetch Theme Data"}
        </button>

        {error && (
          <div class='mt-4 rounded-md bg-red-50 p-4'>
            <p class='text-sm text-red-700'>{error}</p>
          </div>
        )}

        {themeData && (
          <div class='mt-6'>
            <h3 class='mb-2 text-lg font-medium text-gray-100'>Theme Data</h3>
            <pre class='mt-2 overflow-auto rounded-md bg-gray-800 p-4 text-sm text-gray-300'>
              {JSON.stringify(themeData, null, 2)}
            </pre>
          </div>
        )}
      </div>
    </div>
  );
}
