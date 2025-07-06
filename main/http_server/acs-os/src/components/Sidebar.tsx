import { ComponentChildren } from "preact";
import { useState, useContext, useEffect } from "preact/hooks";
import { useTheme } from "../context/ThemeContext";
import { getSystemInfo } from "../utils/api";
import { logger } from "../utils/logger";
import {
  Activity,
  Layers,
  Users,
  Settings,
  Home,
  PanelLeft,
  ChartBar,
  Download,
  Wifi,
  HelpCircle,
} from "lucide-preact";
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
  isMobile: boolean;
  setCollapsed: (collapsed: boolean) => void;
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
    label: "Analytics",
    href: "/analytics",
    icon: ChartBar,
  },
  {
    label: "Logs",
    href: "/logs",
    icon: Activity,
  },
  // {
  //   label: "Theme",
  //   href: "/theme",
  //   icon: Palette,
  // },
  {
    label: "Wifi",
    href: "/wifi",
    icon: Wifi,
  },
  {
    label: "Settings",
    href: "/settings",
    icon: Settings,
  },
  {
    label: "Updates",
    href: "/updates",
    icon: Download,
  },
  {
    label: "Support",
    href: "https://help.advancedcryptoservices.com/en/collections/12812285-bitaxe-support",
    icon: HelpCircle,
  },
];

function NavItem({
  icon: Icon,
  label,
  href = "#",
  active,
  collapsed,
  isMobile,
  setCollapsed,
  onClick,
}: NavItemProps) {
  return (
    <a
      href={href}
      onClick={(e) => {
        // On mobile, collapse the sidebar when a link is clicked
        if (isMobile) {
          setCollapsed(true);
        }
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
  const { getThemeLogo, getThemeFavicon } = useTheme();
  const [isWifiConnected, setIsWifiConnected] = useState(false);
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
  // Track if we're on mobile
  const [isMobile, setIsMobile] = useState(() =>
    typeof window !== "undefined" ? window.innerWidth < 768 : false
  );

  // Fetch WiFi status
  useEffect(() => {
    const fetchWifiStatus = async () => {
      try {
        const systemInfo = await getSystemInfo();
        setIsWifiConnected(systemInfo.wifiStatus === "Connected!");
      } catch (error) {
        logger.error("Failed to fetch WiFi status:", error);
        setIsWifiConnected(false);
      }
    };

    // Fetch immediately
    fetchWifiStatus();

    // Set up periodic refresh every 30 seconds
    const interval = setInterval(fetchWifiStatus, 30000);

    return () => clearInterval(interval);
  }, []);

  useEffect(() => {
    function handleResize() {
      setIsMobile(window.innerWidth < 768);
    }
    window.addEventListener("resize", handleResize);
    return () => window.removeEventListener("resize", handleResize);
  }, []);

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
        "fixed left-0 top-0 z-40 h-screen transform text-slate-200 transition-all duration-300 sm:translate-x-0",
        collapsed ? "w-[70px]" : "w-[240px]"
      )}
      style={{ backgroundColor: '#121319' }}
    >
      <div className='flex h-full flex-col overflow-y-auto border-r border-slate-800'>
        <div className='h-16 flex items-center justify-start px-6 pt-6'>
          {/* On mobile and sidebar open, show only favicon. Otherwise, show logo or favicon as before. Only favicon is clickable to toggle. */}
          {collapsed || (isMobile && !collapsed) ? (
            <div className='flex justify-center w-full'>
              <img
                src={getThemeFavicon()}
                alt='Favicon'
                className='h-4 w-4 cursor-pointer hover:opacity-80 focus:outline-none focus:ring-2 focus:ring-blue-400'
                tabIndex={0}
                aria-label='Toggle sidebar'
                onClick={() => setCollapsed(!collapsed)}
                onKeyDown={(e) => {
                  if (e.key === "Enter" || e.key === " ") setCollapsed(!collapsed);
                }}
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

        <div className='flex-1 py-10'>
          <nav className='space-y-1 px-2'>
            {navItems.map((item) => (
              <NavItem
                key={item.href}
                icon={item.icon}
                label={item.label}
                href={item.href}
                active={activeItem === item.href}
                collapsed={collapsed}
                isMobile={isMobile}
                setCollapsed={setCollapsed}
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
              <div className={`h-2 w-2 rounded-full ${isWifiConnected ? 'bg-emerald-500' : 'bg-red-500'}`} />
              {!collapsed && <span>{isWifiConnected ? 'Connected' : 'Offline'}</span>}
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
