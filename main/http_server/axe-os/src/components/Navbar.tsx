interface NavbarProps {
  title?: string;
}

export function Navbar({ title = "Dashboard" }: NavbarProps) {
  return (
    <nav class='fixed left-0 right-0 top-0 z-30 h-16 border-b border-gray-800 bg-gray-900 pl-64'>
      <div class='flex h-full items-center justify-between px-4'>
        <h1 class='text-xl font-semibold text-gray-100'>{title}</h1>

        <div class='flex items-center gap-4'>
          {/* Status indicator */}
          <div class='flex items-center gap-2'>
            <div class='h-2 w-2 rounded-full bg-green-500'></div>
            <span class='text-sm text-gray-300'>Connected</span>
          </div>

          {/* Settings button */}
          <button
            class='rounded-lg p-2 text-gray-300 hover:bg-gray-800'
            onClick={() => (window.location.href = "/settings")}
          >
            <svg class='h-5 w-5' fill='none' stroke='currentColor' viewBox='0 0 24 24'>
              <path
                stroke-linecap='round'
                stroke-linejoin='round'
                stroke-width='2'
                d='M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z'
              ></path>
              <path
                stroke-linecap='round'
                stroke-linejoin='round'
                stroke-width='2'
                d='M15 12a3 3 0 11-6 0 3 3 0 016 0z'
              ></path>
            </svg>
          </button>
        </div>
      </div>
    </nav>
  );
}
