import { JSX } from "preact/jsx-runtime";

interface SwitchProps {
  checked: boolean;
  onCheckedChange: (checked: boolean) => void;
  disabled?: boolean;
  className?: string;
  id?: string;
  "aria-label"?: string;
  "aria-labelledby"?: string;
  "aria-describedby"?: string;
}

export function Switch({
  checked,
  onCheckedChange,
  disabled = false,
  className = "",
  id,
  ...ariaProps
}: SwitchProps) {
  const handleClick = () => {
    if (!disabled) {
      onCheckedChange(!checked);
    }
  };

  const handleKeyDown = (event: KeyboardEvent) => {
    if (event.key === " " || event.key === "Enter") {
      event.preventDefault();
      handleClick();
    }
  };

  return (
    <button
      type='button'
      role='switch'
      aria-checked={checked}
      disabled={disabled}
      onClick={handleClick}
      onKeyDown={handleKeyDown}
      id={id}
      className={`
        peer inline-flex h-6 w-11 shrink-0 cursor-pointer items-center rounded-full border-2 border-transparent
        transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--primary-color)]
        focus-visible:ring-offset-2 focus-visible:ring-offset-[#0B1C3F] disabled:cursor-not-allowed disabled:opacity-50
        ${checked ? "bg-[var(--primary-color)]" : "bg-slate-700/50 border-slate-600"}
        ${className}
      `}
      {...ariaProps}
    >
      <span
        className={`
          pointer-events-none block h-5 w-5 rounded-full bg-white shadow-lg ring-0 transition-transform
          ${checked ? "translate-x-5" : "translate-x-0"}
        `}
      />
    </button>
  );
}
