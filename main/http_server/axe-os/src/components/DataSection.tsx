import { ComponentChildren } from "preact";
import { DataCard } from "./DataCard";

interface CardData {
  title: string;
  value: string | number | null;
  className?: string;
}

interface DataSectionProps {
  title: string;
  cards: CardData[];
  loading: boolean;
  error: string | null;
  className?: string;
}

export function DataSection({ title, cards, loading, error, className = "" }: DataSectionProps) {
  return (
    <div class={`rounded-lg bg-gray-900 p-6 ${className}`}>
      <h2 class='mb-4 text-xl font-semibold text-gray-100'>{title}</h2>
      {loading ? (
        <div class='text-gray-400'>Loading {title.toLowerCase()} data...</div>
      ) : error ? (
        <div class='text-red-400'>{error}</div>
      ) : (
        <div class='grid gap-4 sm:grid-cols-2 lg:grid-cols-3'>
          {cards.map((card, index) => (
            <DataCard
              key={index}
              title={card.title}
              value={card.value}
              className={card.className}
            />
          ))}
        </div>
      )}
    </div>
  );
}
