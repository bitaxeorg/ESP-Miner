
import { useRef, useEffect, useState } from "preact/hooks";

interface SliderProps {
  value: number[];
  onValueChange: (value: number[]) => void;
  min?: number;
  max?: number;
  step?: number;
  disabled?: boolean;
  className?: string;
  id?: string;
  "aria-label"?: string;
  "aria-labelledby"?: string;
  "aria-describedby"?: string;
}

export function Slider({
  value,
  onValueChange,
  min = 0,
  max = 100,
  step = 1,
  disabled = false,
  className = "",
  id,
  ...ariaProps
}: SliderProps) {
  const trackRef = useRef<HTMLDivElement>(null);
  const [isDragging, setIsDragging] = useState(false);
  const [dragIndex, setDragIndex] = useState<number | null>(null);

  const currentValue = value[0] ?? min;
  const percentage = ((currentValue - min) / (max - min)) * 100;

  const updateValue = (clientX: number) => {
    if (!trackRef.current || disabled) return;

    const rect = trackRef.current.getBoundingClientRect();
    const percentage = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
    const newValue = min + percentage * (max - min);
    const steppedValue = Math.round(newValue / step) * step;
    const clampedValue = Math.max(min, Math.min(max, steppedValue));

    onValueChange([clampedValue]);
  };

  const handleMouseDown = (event: MouseEvent) => {
    if (disabled) return;

    event.preventDefault();
    setIsDragging(true);
    setDragIndex(0);
    updateValue(event.clientX);
  };

  const handleKeyDown = (event: KeyboardEvent) => {
    if (disabled) return;

    let newValue = currentValue;

    switch (event.key) {
      case "ArrowLeft":
      case "ArrowDown":
        event.preventDefault();
        newValue = Math.max(min, currentValue - step);
        break;
      case "ArrowRight":
      case "ArrowUp":
        event.preventDefault();
        newValue = Math.min(max, currentValue + step);
        break;
      case "Home":
        event.preventDefault();
        newValue = min;
        break;
      case "End":
        event.preventDefault();
        newValue = max;
        break;
      case "PageDown":
        event.preventDefault();
        newValue = Math.max(min, currentValue - step * 10);
        break;
      case "PageUp":
        event.preventDefault();
        newValue = Math.min(max, currentValue + step * 10);
        break;
      default:
        return;
    }

    onValueChange([newValue]);
  };

  useEffect(() => {
    const handleMouseMove = (event: MouseEvent) => {
      if (isDragging && dragIndex !== null) {
        updateValue(event.clientX);
      }
    };

    const handleMouseUp = () => {
      setIsDragging(false);
      setDragIndex(null);
    };

    if (isDragging) {
      document.addEventListener("mousemove", handleMouseMove);
      document.addEventListener("mouseup", handleMouseUp);
    }

    return () => {
      document.removeEventListener("mousemove", handleMouseMove);
      document.removeEventListener("mouseup", handleMouseUp);
    };
  }, [isDragging, dragIndex]);

  return (
    <div
      className={`
        relative flex w-full touch-none select-none items-center
        ${disabled ? "opacity-50 cursor-not-allowed" : "cursor-pointer"}
        ${className}
      `}
      onMouseDown={handleMouseDown}
    >
      {/* Track */}
      <div
        ref={trackRef}
        className='relative h-2 w-full grow overflow-hidden rounded-full bg-slate-700/50 border border-slate-600'
      >
        {/* Range */}
        <div
          className='absolute h-full bg-[var(--primary-color)] transition-all duration-150'
          style={{ width: `${percentage}%` }}
        />
      </div>

      {/* Thumb */}
      <div
        className='absolute h-5 w-5 -translate-x-1/2 rounded-full border-2 border-[var(--primary-color)] bg-white shadow-lg ring-offset-[#0B1C3F] transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[var(--primary-color)] focus-visible:ring-offset-2 disabled:pointer-events-none disabled:opacity-50'
        style={{ left: `${percentage}%` }}
        tabIndex={disabled ? -1 : 0}
        role='slider'
        aria-valuemin={min}
        aria-valuemax={max}
        aria-valuenow={currentValue}
        aria-orientation='horizontal'
        onKeyDown={handleKeyDown}
        id={id}
        {...ariaProps}
      />
    </div>
  );
}
