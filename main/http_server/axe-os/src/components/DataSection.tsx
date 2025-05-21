import { Gauge, LucideIcon } from "lucide-preact";
import { DataCard } from "./DataCard";
import { ComponentType } from "preact";

interface CardData {
  title: string;
  value: string | number | null;
  subtitle?: string;
  percentageChange?: number;
  className?: string;
  icon?: LucideIcon;
}

interface DataSectionProps {
  title: string;
  cards: CardData[];
  loading: boolean;
  error: string | null;
  className?: string;
  icon?: LucideIcon;
}

export function DataSection({
  title,
  icon: Icon,
  cards,
  loading,
  error,
  className = "",
}: DataSectionProps) {
  return (
    <section className={`space-y-4 px-1 ${className}`}>
      {loading ? (
        <div className='text-slate-400'>Loading {title.toLowerCase()} data...</div>
      ) : error ? (
        <div className='text-[#FF2C47]'>{error}</div>
      ) : (
        <>
          <div className='flex items-center gap-2 pt-6 pl-0.5'>
            {Icon && <Icon className='h-5 w-5 text-blue-500' />}
            <h2 className='text-lg font-bold text-white'>{title}</h2>
          </div>
          <div className='grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 lg:grid-cols-4 gap-3 md:gap-4 pr-1 w-full'>
            {cards.map((card, index) => (
              <DataCard key={index} {...card} />
            ))}
          </div>
        </>
      )}
    </section>
  );
}
