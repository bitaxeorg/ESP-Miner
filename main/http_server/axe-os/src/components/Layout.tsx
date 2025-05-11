import { ComponentChildren } from "preact";
import { Sidebar } from "./Sidebar";
import { Navbar } from "./Navbar";

interface LayoutProps {
  children: ComponentChildren;
  title?: string;
}

export function Layout({ children, title }: LayoutProps) {
  return (
    <div class='min-h-screen bg-gray-950'>
      <Sidebar />
      <Navbar title={title} />

      {/* Main content */}
      <main class='pl-64 pt-16'>
        <div class='container mx-auto p-6'>{children}</div>
      </main>
    </div>
  );
}
