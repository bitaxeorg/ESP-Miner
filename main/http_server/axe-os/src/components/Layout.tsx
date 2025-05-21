import { ComponentChildren } from "preact";
import { Sidebar } from "./Sidebar";
import { Navbar } from "./Navbar";
import { createContext } from "preact";
import { useContext, useState, useEffect } from "preact/hooks";

interface LayoutProps {
  children: ComponentChildren;
  title?: string;
}

// Create a context for the sidebar state
interface SidebarContextType {
  collapsed: boolean;
  setCollapsed: (collapsed: boolean) => void;
}

export const SidebarContext = createContext<SidebarContextType>({
  collapsed: false,
  setCollapsed: () => {},
});

export function Layout({ children, title }: LayoutProps) {
  // Initialize with a default value based on window width if available
  const [collapsed, setCollapsed] = useState(false);

  // Effect to check for mobile and set default collapsed state
  useEffect(() => {
    // Check for mobile: common breakpoint for mobile is 768px
    const isMobile = window.innerWidth < 768;
    if (isMobile) {
      setCollapsed(true);
    }

    // Optional: Update on resize
    const handleResize = () => {
      const isMobile = window.innerWidth < 768;
      if (isMobile && !collapsed) {
        setCollapsed(true);
      }
    };

    window.addEventListener("resize", handleResize);
    return () => window.removeEventListener("resize", handleResize);
  }, []);

  return (
    <SidebarContext.Provider value={{ collapsed, setCollapsed }}>
      <div class='min-h-screen min-w-full overflow-x-hidden bg-gray-950'>
        <Sidebar />
        <Navbar title={title} />

        {/* Main content - adjust padding based on sidebar state */}
        <main
          class={`pt-16 transition-all duration-300 ${
            collapsed ? "pl-[50px] md:pl-[70px]" : "pl-[50px] md:pl-[240px]"
          } bg-gray-950 min-w-full`}
        >
          <div class='px-3 py-3 md:px-6 md:py-4 max-w-xs md:max-w-full'>{children}</div>
        </main>
      </div>
    </SidebarContext.Provider>
  );
}
