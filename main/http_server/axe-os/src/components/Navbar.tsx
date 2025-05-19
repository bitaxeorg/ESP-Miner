interface NavbarProps {
  title?: string;
}

export function Navbar({ title = "Dashboard" }: NavbarProps) {
  return (
    <nav class='fixed left-0 right-0 top-0 z-30 h-16 border-b border-gray-800 bg-slate-900 pl-64'>
      <div class='flex h-full items-center justify-between px-4'>
        <h1 class='text-xl font-semibold text-gray-100'>{title}</h1>
      </div>
    </nav>
  );
}
