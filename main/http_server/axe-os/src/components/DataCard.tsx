import { ComponentChildren } from "preact";

interface DataCardProps {
  title: string;
  value: string | number | null;
  className?: string;
}

export function DataCard({ title, value, className = "" }: DataCardProps) {
  return (
    <div class={`rounded-lg bg-gray-800 p-4 ${className}`}>
      <h3 class='text-sm font-medium text-gray-400'>{title}</h3>
      <p class='mt-2 text-2xl font-semibold text-gray-100'>
        {value !== null && value !== undefined ? value : "--"}
      </p>
    </div>
  );
}
