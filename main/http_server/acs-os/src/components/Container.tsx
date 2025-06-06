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
      className={`container mx-auto w-full py-6 pt-0 text-left rounded-lg  text-gray-100 overflow-x-auto ${className}`}
    >
      {children}
    </div>
  );
}
