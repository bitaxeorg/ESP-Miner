import { ComponentChildren } from "preact";
import { useState, useContext } from "preact/hooks";
import { useTheme } from "../context/ThemeContext";
import { Activity, Layers, Users, Palette, Settings, Home, PanelLeft } from "lucide-preact";
import { SidebarContext } from "./Layout";

interface SidebarProps {
  children?: ComponentChildren;
}

interface NavItemProps {
  icon: any;
  label: string;
  href?: string;
  active?: boolean;
  collapsed: boolean;
}

const navItems = [
  {
    label: "Dashboard",
    href: "/",
    icon: Home,
    active: true,
  },
  {
    label: "Miners",
    href: "/miners",
    icon: Layers,
  },
  {
    label: "Pool",
    href: "/pool",
    icon: Users,
  },
  {
    label: "Analytics",
    href: "/analytics",
    icon: Activity,
  },
  {
    label: "Theme",
    href: "/theme",
    icon: Palette,
  },
  {
    label: "Settings",
    href: "/settings",
    icon: Settings,
  },
];

function NavItem({ icon: Icon, label, href = "#", active, collapsed }: NavItemProps) {
  return (
    <a
      href={href}
      className={`flex items-center rounded-lg px-4 py-2 ${
        active ? "bg-slate-800 text-white" : "text-slate-400 hover:bg-slate-800 hover:text-white"
      } ${collapsed ? "justify-center" : ""}`}
    >
      <Icon className='h-5 w-5' />
      {!collapsed && <span className='ml-3'>{label}</span>}
    </a>
  );
}

function cn(...classes: string[]) {
  return classes.filter(Boolean).join(" ");
}

export function Sidebar({ children }: SidebarProps) {
  const { collapsed, setCollapsed } = useContext(SidebarContext);
  const [logoLoaded, setLogoLoaded] = useState(true);
  const { getThemeLogo } = useTheme();

  return (
    <aside
      className={cn(
        "fixed left-0 top-0 z-40 h-screen transform bg-slate-900 text-slate-200 transition-all duration-300 sm:translate-x-0",
        collapsed ? "w-[70px]" : "w-[240px]"
      )}
    >
      <div className='flex h-full flex-col overflow-y-auto border-r border-slate-800'>
        <div className='h-16 border-b border-slate-800 flex items-center justify-center px-3'>
          {collapsed ? (
            <div className='flex justify-center w-full'>
              <img
                src='/american-btc-small.png'
                alt='Favicon'
                className='h-8 w-8'
                onLoad={() => setLogoLoaded(true)}
              />
            </div>
          ) : (
            <div className='flex items-center gap-2'>
              <img
                src={getThemeLogo()}
                alt='Logo'
                className='h-8'
                onLoad={() => setLogoLoaded(true)}
              />
            </div>
          )}
        </div>

        <div className='flex-1 py-4'>
          <nav className='space-y-1 px-2'>
            {navItems.map((item) => (
              <NavItem
                key={item.href}
                icon={item.icon}
                label={item.label}
                href={item.href}
                active={item.active}
                collapsed={collapsed}
              />
            ))}
          </nav>
        </div>

        {/* Status indicator and collapse button */}
        <div className='p-4 border-t border-slate-800'>
          <div
            className={cn(
              "flex items-center gap-3 text-sm text-slate-400",
              collapsed ? "justify-center" : "justify-between"
            )}
          >
            <div className='flex items-center gap-3'>
              <div className='h-2 w-2 rounded-full bg-emerald-500'></div>
              {!collapsed && <span>Connected</span>}
            </div>
            <button
              onClick={() => setCollapsed(!collapsed)}
              className='h-8 w-8 text-slate-400 hover:text-white hover:bg-slate-800 rounded flex items-center justify-center'
            >
              <PanelLeft className='h-4 w-4' />
            </button>
          </div>
        </div>
      </div>
    </aside>
  );
}
