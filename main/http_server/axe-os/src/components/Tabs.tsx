import { ComponentChildren } from "preact";

interface TabsProps {
  tabs: {
    id: string;
    label: string;
  }[];
  activeTab: string;
  onTabChange: (tabId: string) => void;
}

export function Tabs({ tabs, activeTab, onTabChange }: TabsProps) {
  return (
    <div class='w-full flex justify-start mb-4'>
      <div class='inline-flex space-x-1 rounded-lg bg-slate-800/50 p-1 border border-slate-700'>
        {tabs.map((tab) => (
          <button
            key={tab.id}
            onClick={() => onTabChange(tab.id)}
            class={`
              px-6 py-2.5 text-sm font-semibold rounded-md transition-all cursor-pointer
              ${
                activeTab === tab.id
                  ? "bg-blue-900/50 text-blue-400"
                  : "text-slate-400 hover:bg-blue-900/50 hover:text-blue-400"
              }
            `}
          >
            {tab.label}
          </button>
        ))}
      </div>
    </div>
  );
}
