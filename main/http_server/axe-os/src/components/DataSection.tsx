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
    <section className='space-y-4'>
      {loading ? (
        <div className='text-slate-400'>Loading {title.toLowerCase()} data...</div>
      ) : error ? (
        <div className='text-[#FF2C47]'>{error}</div>
      ) : (
        <>
          <div className='flex items-center gap-2 pt-6'>
            {Icon && <Icon className='h-5 w-5 text-blue-500' />}
            <h2 className='text-lg font-bold text-white'>{title}</h2>
          </div>
          <div className='grid grid-cols-1 md:grid-cols-3 gap-4'>
            {cards.map((card, index) => (
              <DataCard key={index} {...card} />
            ))}
          </div>
        </>
      )}
    </section>
  );
}
