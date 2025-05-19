import { ComponentChildren, ComponentType } from "preact";
import { LucideIcon } from "lucide-preact";

interface DataCardProps {
  title: string;
  value: string | number | null;
  subtitle?: string;
  percentageChange?: number;
  icon?: LucideIcon;
  className?: string;
  color?: "blue" | "green" | "red";
}

const colorVariants = {
  blue: "bg-blue-500/10 text-blue-500",
  green: "bg-green-500/10 text-green-500",
  red: "bg-red-500/10 text-red-500",
};

export function DataCard({
  title,
  value,
  subtitle,
  percentageChange,
  icon: Icon,
  color = "blue",
  className = "",
}: DataCardProps) {
  return (
    <div
      className={`rounded-xl bg-[#0B1C3F]/80 border border-[#1C2F52] p-6 backdrop-blur-sm ${className}`}
    >
      <div className='flex justify-between items-start'>
        <div className='space-y-4'>
          <h3 className='text-white/90 text-base font-medium'>{title}</h3>
          <div>
            <div className='flex items-baseline gap-1'>
              <p className='text-3xl font-bold text-white'>
                {value !== null && value !== undefined ? value : "--"}
              </p>
            </div>
            {subtitle && <p className='mt-1 text-sm text-[#8B96A5]'>{subtitle}</p>}
            {percentageChange !== undefined && (
              <div className='mt-2'>
                <span
                  className={`text-sm ${percentageChange >= 0 ? "text-green-400" : "text-red-400"}`}
                >
                  {percentageChange >= 0 ? "↑" : "↓"} {Math.abs(percentageChange)}%
                </span>
              </div>
            )}
          </div>
        </div>
        {Icon && (
          <div className={`rounded-full p-3 ${colorVariants[color]}`}>
            <Icon className='h-5 w-5' />
          </div>
        )}
      </div>
    </div>
  );
}
