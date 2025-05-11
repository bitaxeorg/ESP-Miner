import { useTheme } from "../../context/ThemeContext";
import { Button } from "../../components/button";

export function Theme() {
  const { themeData, loading, error, fetchTheme } = useTheme();

  return (
    <div class='space-y-6'>
      <div class='rounded-lg bg-gray-900 p-6'>
        <h2 class='mb-4 text-xl font-semibold text-gray-100'>Theme Configuration</h2>

        <div class='mb-6'>
          <Button onClick={fetchTheme} disabled={loading} variant='default' size='default'>
            {loading ? "Loading..." : "Refresh Theme Data"}
          </Button>

          <div class='mt-4'>
            <p class='text-gray-300'>
              Current theme: <span class='font-semibold'>{themeData?.theme || "Not loaded"}</span>
            </p>
            <p class='text-gray-300'>
              Color scheme:{" "}
              <span class='font-semibold'>{themeData?.colorScheme || "Not loaded"}</span>
            </p>
          </div>
        </div>

        {error && (
          <div class='mt-4 rounded-md bg-red-50 p-4'>
            <p class='text-sm text-red-700'>{error}</p>
          </div>
        )}

        {themeData && (
          <div class='mt-6'>
            <h3 class='mb-4 text-lg font-medium text-gray-100'>Theme Preview</h3>

            <div class='grid gap-6 sm:grid-cols-2'>
              <div class='space-y-4'>
                <div class='rounded-md bg-gray-800 p-4'>
                  <h4 class='mb-2 text-sm font-medium text-gray-400'>Button Variants</h4>
                  <div class='flex flex-wrap gap-2'>
                    <Button variant='default'>Default</Button>
                    <Button variant='outline'>Outline</Button>
                    <Button variant='ghost'>Ghost</Button>
                    <Button variant='link'>Link</Button>
                  </div>
                </div>

                <div class='rounded-md bg-gray-800 p-4'>
                  <h4 class='mb-2 text-sm font-medium text-gray-400'>Button Sizes</h4>
                  <div class='flex flex-wrap items-center gap-2'>
                    <Button size='sm'>Small</Button>
                    <Button size='default'>Default</Button>
                    <Button size='lg'>Large</Button>
                    <Button size='icon'>
                      <svg
                        xmlns='http://www.w3.org/2000/svg'
                        width='16'
                        height='16'
                        viewBox='0 0 24 24'
                        fill='none'
                        stroke='currentColor'
                        stroke-width='2'
                        stroke-linecap='round'
                        stroke-linejoin='round'
                      >
                        <path d='M12 5v14M5 12h14' />
                      </svg>
                    </Button>
                  </div>
                </div>

                <div class='rounded-md bg-gray-800 p-4'>
                  <h4 class='mb-2 text-sm font-medium text-gray-400'>Colors</h4>
                  <div class='grid grid-cols-2 gap-2'>
                    <div class='rounded p-2 primary-bg primary-text'>Primary Background</div>
                    <div class='rounded border primary-border p-2 text-gray-300'>
                      Primary Border
                    </div>
                  </div>
                </div>
              </div>

              <div class='rounded-md bg-gray-800 p-4'>
                <h3 class='mb-2 text-sm font-medium text-gray-400'>Theme Data</h3>
                <pre class='mt-2 max-h-64 overflow-auto rounded-md bg-gray-700 p-3 text-xs text-gray-300'>
                  {JSON.stringify(themeData, null, 2)}
                </pre>
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
