import { ComponentChildren } from "preact";

interface SidebarProps {
  children?: ComponentChildren;
}

const navItems = [
  {
    label: "Dashboard",
    href: "/",
  },
  {
    label: "Miners",
    href: "/miners",
  },
  {
    label: "Theme",
    href: "/theme",
  },
  {
    label: "Settings",
    href: "/settings",
  },
];

export function Sidebar({ children }: SidebarProps) {
  return (
    <aside class='fixed left-0 top-0 z-40 h-screen w-64 transform bg-gray-900 transition-transform sm:translate-x-0'>
      <div class='flex h-full flex-col overflow-y-auto border-r border-gray-800 px-3 py-4'>
        <div class='mb-10 flex items-center px-2'>
          <span class='text-xl font-semibold text-gray-100'>AxeOS</span>
        </div>

        <nav class='flex-1 space-y-2'>
          {navItems.map((item) => (
            <a
              key={item.href}
              href={item.href}
              class='flex items-center rounded-lg px-4 py-2 text-gray-300 hover:bg-gray-800'
            >
              <span>{item.label}</span>
            </a>
          ))}
        </nav>
      </div>
    </aside>
  );
}
