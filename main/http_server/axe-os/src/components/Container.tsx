import { ComponentChildren } from "preact";

export function Container({
  children,
  className = "",
}: {
  children: ComponentChildren;
  className?: string;
}) {
  return (
    <div
      className={`container mx-auto w-full px-4 sm:px-6 py-6 text-left rounded-lg bg-gray-900 text-gray-100 overflow-x-auto ${className}`}
    >
      {children}
    </div>
  );
}
