interface PageHeadingProps {
  title: string;
  subtitle?: string;
  link?: string;
}

export function PageHeading({ title, subtitle, link }: PageHeadingProps) {
  return (
    <div class='mb-6 text-left md:pt-2'>
      <h1 class='text-xl md:text-2xl font-bold text-slate-100 mb-1 text-left'>{title}</h1>
      {subtitle && (
        <p class='text-slate-400 md:text-sm text-xs text-left'>
          {subtitle}
          {link && (
            <>
              {" "}
              <a
                href={link}
                target='_blank'
                rel='noopener noreferrer'
                class='text-blue-400 hover:text-blue-300 font-medium transition-colors'
              >
                Learn more →
              </a>
            </>
          )}
        </p>
      )}
      {!subtitle && link && (
        <a
          href={link}
          target='_blank'
          rel='noopener noreferrer'
          class='text-blue-400 hover:text-blue-300 text-sm font-medium transition-colors inline-block'
        >
          Learn more →
        </a>
      )}
    </div>
  );
}
