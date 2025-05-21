import { useContext } from "preact/hooks";
import { SidebarContext } from "./Layout";
import { useTheme } from "../context/ThemeContext";

interface NavbarProps {
  title?: string;
}

export function Navbar({ title = "Dashboard" }: NavbarProps) {
  const { collapsed, setCollapsed } = useContext(SidebarContext);
  const { getThemeLogo } = useTheme();

  const logoSrc = getThemeLogo();

  return (
    <nav
      class={`fixed right-0 top-0 z-30 h-16 border-b border-gray-800 bg-slate-900 transition-all duration-300 ${
        collapsed ? "left-[70px]" : "left-[240px]"
      }`}
    >
      <div class='flex h-full items-center px-6 py-4'>
        {/* <h1 class='hidden md:block text-xl font-semibold text-gray-100'>{title}</h1> */}
        <img
          src={logoSrc}
          alt='Logo'
          class='md:hidden h-8 w-auto cursor-pointer hover:opacity-80 focus:outline-none focus:ring-2 focus:ring-blue-400 rounded'
          tabIndex={0}
          aria-label='Toggle sidebar'
          onClick={() => setCollapsed(!collapsed)}
          onKeyDown={(e) => {
            if (e.key === "Enter" || e.key === " ") setCollapsed(!collapsed);
          }}
        />
      </div>
    </nav>
  );
}
