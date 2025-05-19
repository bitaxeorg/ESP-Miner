import { ComponentChildren } from "preact";

export function Container({ children }: { children: ComponentChildren }) {
  return (
    <div className='container mx-auto text-left py-6 rounded-lg bg-gray-900 p-6 text-gray-100'>
      {children}
    </div>
  );
}
