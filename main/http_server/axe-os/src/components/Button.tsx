import { ComponentChildren } from "preact";

interface ButtonProps {
  children: ComponentChildren;
  variant?: "default" | "outline" | "ghost" | "link";
  size?: "default" | "sm" | "lg" | "icon";
  disabled?: boolean;
  onClick?: (event: MouseEvent) => void;
  type?: "button" | "submit" | "reset";
  className?: string;
  [key: string]: any;
}

export function Button({
  children,
  variant = "default",
  size = "default",
  disabled = false,
  onClick,
  type = "button",
  className = "",
  ...props
}: ButtonProps) {
  // Base styles
  let baseStyles =
    "inline-flex items-center justify-center rounded-md font-medium transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-offset-2 disabled:pointer-events-none disabled:opacity-50";

  // Apply variant styling
  const variantStyles = {
    default:
      "bg-[var(--primary-color)] text-[var(--primary-color-text)] hover:bg-[color-mix(in_srgb,var(--primary-color),black_10%)] shadow focus-visible:ring-[var(--primary-color)]",
    outline:
      "border border-[var(--primary-color)] bg-transparent text-[var(--primary-color)] hover:bg-[var(--primary-color)] hover:text-[var(--primary-color-text)]",
    ghost:
      "bg-transparent text-[var(--primary-color)] hover:bg-[color-mix(in_srgb,var(--primary-color),transparent_90%)] hover:text-[var(--primary-color)]",
    link: "bg-transparent text-[var(--primary-color)] underline-offset-4 hover:underline p-0 h-auto",
  };

  // Apply size styling
  const sizeStyles = {
    default: "h-10 px-4 py-2",
    sm: "h-8 rounded-md px-3 text-xs",
    lg: "h-12 rounded-md px-8 text-base",
    icon: "h-10 w-10",
  };

  // Combine all styles
  const buttonStyles = `${baseStyles} ${variantStyles[variant]} ${sizeStyles[size]} ${className}`;

  return (
    <button type={type} disabled={disabled} onClick={onClick} class={buttonStyles} {...props}>
      {children}
    </button>
  );
}
