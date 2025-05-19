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

/**
 * Separates a value string into its numeric part and unit
 * Examples: "123.45 TH/s" -> ["123.45", "TH/s"]
 *          "42.0W" -> ["42.0", "W"]
 *          "3.14V" -> ["3.14", "V"]
 */
function parseValueAndUnit(value: string): [string, string | null] {
  // Common units we want to separate
  const units = ["TH/s", "W", "V", "°C", "MHz", "mV", "%", "MB"];

  // If the value is just a number, return it with no unit
  if (!isNaN(Number(value))) {
    return [value, null];
  }

  // Try to find any of our known units at the end of the string
  for (const unit of units) {
    if (value.endsWith(unit)) {
      const numericPart = value.slice(0, -unit.length).trim();
      return [numericPart, unit];
    }
  }

  // If no known unit is found, return the original value with no unit
  return [value, null];
}

export function DataCard({
  title,
  value,
  subtitle,
  percentageChange,
  icon: Icon,
  color = "blue",
  className = "",
}: DataCardProps) {
  const displayValue = value !== null && value !== undefined ? value.toString() : "--";
  const [numericValue, unit] =
    displayValue !== "--" ? parseValueAndUnit(displayValue) : [displayValue, null];

  return (
    <div
      className={`w-56 rounded-xl bg-[#0B1C3F]/80 border border-[#1C2F52] p-6 backdrop-blur-sm ${className}`}
    >
      <div className='flex justify-between gap-6'>
        <div className='flex-1 min-w-0 space-y-2'>
          <div className='flex items-center justify-between gap-6'>
            <h3 className='text-sm font-medium text-slate-200 text-left truncate'>{title}</h3>
            {Icon && (
              <div className={`rounded-full p-1.5 ${colorVariants[color]} shrink-0`}>
                <Icon className='h-4 w-4' />
              </div>
            )}
          </div>
          <div>
            <div className='flex items-baseline gap-1'>
              <p className='text-2xl font-bold text-white'>{numericValue}</p>
              {unit && <span className='text-lg text-[#8B96A5]'>{unit}</span>}
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
      </div>
    </div>
  );
}
