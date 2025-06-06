import { Container } from "../../components/Container";
import { PageHeading } from "../../components/PageHeading";

export function Theme() {
  return (
    <Container>
      <PageHeading
        title='Themes'
        subtitle='Theme functionality is temporarily disabled.'
        link='https://help.advancedcryptoservices.com/en/articles/11517914-themes'
      />
      <div class='rounded-md bg-gray-800 p-6'>
        <div class='text-center'>
          <div class='mb-4'>
            <svg class='mx-auto h-12 w-12 text-gray-400' fill='none' viewBox='0 0 24 24' stroke='currentColor'>
              <path stroke-linecap='round' stroke-linejoin='round' stroke-width='2' d='M9.663 17h4.673M12 3v1m6.364 1.636l-.707.707M21 12h-1M4 12H3m3.343-5.657l-.707-.707m2.828 9.9a5 5 0 117.072 0l-.548.547A3.374 3.374 0 0014 18.469V19a2 2 0 11-4 0v-.531c0-.895-.356-1.754-.988-2.386l-.548-.547z' />
            </svg>
          </div>
          <h3 class='text-lg font-medium text-gray-100 mb-2'>Themes Temporarily Disabled</h3>
          <p class='text-gray-400 mb-4'>
            Theme customization is currently disabled for maintenance.
            The interface is using the default theme.
          </p>
          <div class='bg-gray-700 rounded-md p-4'>
            <p class='text-sm text-gray-300'>
              Current theme: <span class='font-mono text-blue-400'>THEME_ACS_DEFAULT</span>
            </p>
          </div>
        </div>
      </div>
    </Container>
  );
}
