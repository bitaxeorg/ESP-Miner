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
    <div class='w-full mb-4'>
      {/* Dropdown for xs screens */}
      <select
        class='block sm:hidden w-full rounded-lg bg-slate-800/50 p-2 border border-slate-700 text-slate-200 focus:ring-2 focus:ring-blue-400'
        value={activeTab}
        onChange={(e) => onTabChange((e.target as HTMLSelectElement).value)}
      >
        {tabs.map((tab) => (
          <option key={tab.id} value={tab.id}>
            {tab.label}
          </option>
        ))}
      </select>
      {/* Tabs for sm+ screens */}
      <div class='hidden sm:flex w-full justify-start'>
        <div class='inline-flex space-x-1 rounded-lg bg-slate-800/50 px-0 border border-slate-700 overflow-x-auto whitespace-nowrap scrollbar-thin scrollbar-thumb-slate-700'>
          {tabs.map((tab) => (
            <button
              key={tab.id}
              onClick={() => onTabChange(tab.id)}
              class={`
                px-6 py-2.5 text-sm font-semibold rounded-md transition-all cursor-pointer min-w-20
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
    </div>
  );
}
