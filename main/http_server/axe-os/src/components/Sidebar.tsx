import { ComponentChildren } from "preact";
import { useState, useContext, useEffect } from "preact/hooks";
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
  onClick?: () => void;
}

const navItems = [
  {
    label: "Dashboard",
    href: "/",
    icon: Home,
  },
  {
    label: "Miners",
    href: "/miners",
    icon: Layers,
  },
  {
    label: "Pool",
    href: "/pools",
    icon: Users,
  },
  {
    label: "Logs",
    href: "/logs",
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

function NavItem({ icon: Icon, label, href = "#", active, collapsed, onClick }: NavItemProps) {
  return (
    <a
      href={href}
      onClick={(e) => {
        if (onClick) onClick();
      }}
      className={`flex items-center font-semibold rounded-lg px-4 py-2 ${
        active
          ? "bg-blue-900/50 text-blue-400 font-medium"
          : "text-slate-400 hover:text-white hover:bg-blue-900/50"
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
  const [activeItem, setActiveItem] = useState(() => {
    // Initialize based on current path
    if (typeof window !== "undefined") {
      const path = window.location.pathname;
      // Find the matching nav item or default to dashboard
      const match = navItems.find(
        (item) => item.href === path || (item.href !== "/" && path.startsWith(item.href))
      );
      return match?.href || "/";
    }
    return "/";
  });

  useEffect(() => {
    // Update active item when URL changes
    const handleLocationChange = () => {
      const path = window.location.pathname;
      const match = navItems.find(
        (item) => item.href === path || (item.href !== "/" && path.startsWith(item.href))
      );
      if (match) {
        setActiveItem(match.href);
      }
    };

    // Call once on mount
    handleLocationChange();

    // Listen for navigation events
    window.addEventListener("popstate", handleLocationChange);

    return () => {
      window.removeEventListener("popstate", handleLocationChange);
    };
  }, []);

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
                active={activeItem === item.href}
                collapsed={collapsed}
                onClick={() => setActiveItem(item.href)}
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
