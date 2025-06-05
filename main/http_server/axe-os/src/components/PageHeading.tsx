interface PageHeadingProps {
  title: string;
  subtitle?: string;
}

export function PageHeading({ title, subtitle }: PageHeadingProps) {
  return (
    <div class="mb-6 text-left pt-2">
      <h1 class="text-xl md:text-2xl font-bold text-slate-100 mb-1 text-left">
        {title}
      </h1>
      {subtitle && (
        <p class="text-slate-400 text-sm text-left">
          {subtitle}
        </p>
      )}
    </div>
  );
}