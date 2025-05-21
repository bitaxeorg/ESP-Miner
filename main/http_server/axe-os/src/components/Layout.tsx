import { ComponentChildren } from "preact";
import { Sidebar } from "./Sidebar";
import { Navbar } from "./Navbar";
import { createContext } from "preact";
import { useContext, useState } from "preact/hooks";

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
  const [collapsed, setCollapsed] = useState(false);

  return (
    <SidebarContext.Provider value={{ collapsed, setCollapsed }}>
      <div class='min-h-screen bg-gray-950'>
        <Sidebar />
        <Navbar title={title} />

        {/* Main content - adjust padding based on sidebar state */}
        <main class={`pt-16 transition-all duration-300 ${collapsed ? "pl-[0px]" : "pl-[160px]"}`}>
          <div class='px-6 py-4'>{children}</div>
        </main>
      </div>
    </SidebarContext.Provider>
  );
}
