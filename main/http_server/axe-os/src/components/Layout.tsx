import { ComponentChildren } from "preact";
import { Sidebar } from "./Sidebar";
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
      <div class='min-h-screen overflow-x-hidden' style={{ backgroundColor: "#121319" }}>
        <Sidebar />

        {/* Main content */}
        <main
          class={`transition-all duration-300 ${
            collapsed ? "pl-[70px] md:pl-[40px]" : "pl-[180px] md:pl-[180px]"
          }`}
          style={{ backgroundColor: "#121319" }}
        >
          <div class='py-0 md:px-2 md:pt-4 w-full'>{children}</div>
        </main>
      </div>
    </SidebarContext.Provider>
  );
}
