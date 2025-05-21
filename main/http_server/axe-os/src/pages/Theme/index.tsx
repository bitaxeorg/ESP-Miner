import { useTheme } from "../../context/ThemeContext";
import { ThemeSelector } from "../../components/ThemeSelector";
import { Container } from "../../components/Container";

export function Theme() {
  const { themeData } = useTheme();

  return (
    <Container>
      <div class='rounded-lg bg-gray-900 p-6 text-gray-100'>
        <h1 class='text-2xl font-bold mb-6'>Available Themes</h1>
        <div class='rounded-md bg-gray-800 p-4'>
          <ThemeSelector />
        </div>

        {themeData && (
          <div class='mt-6'>
            <h3 class='mb-4 text-lg font-medium text-gray-100'>Theme Preview</h3>
            <div class='grid gap-6 sm:grid-cols-2'>
              <div class='space-y-4'>
                <div class='rounded-md bg-gray-800 p-4'>
                  <pre class='text-sm text-white min-h-[200px]'>
                    {JSON.stringify(themeData, null, 2)}
                  </pre>
                </div>
              </div>
              <div class='rounded-md bg-gray-800 p-4'>
                <ul class='space-y-2'>
                  {Object.entries(themeData || {})
                    .filter(([key]) => key !== "themeName")
                    .map(([key, value]) => (
                      <li key={key} class='flex items-center'>
                        <span class='text-gray-300'>{key.replace(/Color$/, "")}: </span>
                        <code class='ml-1 text-gray-400 text-xs'>
                          {typeof value === "string" ? value : JSON.stringify(value)}
                        </code>
                        <div
                          class='h-4 w-4 rounded-full ml-2'
                          style={{ backgroundColor: typeof value === "string" ? value : "#333" }}
                        ></div>
                      </li>
                    ))}
                </ul>
              </div>
            </div>
          </div>
        )}
      </div>
    </Container>
  );
}
