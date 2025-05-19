import { ComponentChildren } from "preact";

interface DataCardProps {
  title: string;
  value: string | number | null;
  className?: string;
}

export function DataCard({ title, value, className = "" }: DataCardProps) {
  return (
    <div class={`rounded-xl bg-[#0B1C3F] p-4 shadow-inner ${className}`}>
      <h3 class='text-sm text-white'>{title}</h3>
      <p class='mt-2 text-2xl font-bold'>{value !== null && value !== undefined ? value : "--"}</p>
    </div>
  );
}
