import { h } from "preact";
import { Toast as ToastType, ToastType as ToastVariant } from "../context/ToastContext";

interface ToastProps {
  toast: ToastType;
  onClose: (id: string) => void;
}

const TOAST_VARIANTS = {
  success: "bg-green-100 border-green-400 text-green-800",
  error: "bg-red-100 border-red-400 text-red-800",
  info: "bg-blue-100 border-blue-400 text-blue-800",
};

const ICONS = {
  success: "✓",
  error: "✕",
  info: "ℹ",
};

export function Toast({ toast, onClose }: ToastProps) {
  const { id, message, type } = toast;

  return (
    <div
      className={`rounded-md px-4 py-3 mb-2 border-l-4 flex items-center justify-between ${TOAST_VARIANTS[type]}`}
      role='alert'
    >
      <div className='flex items-center'>
        <span className='font-bold mr-2'>{ICONS[type]}</span>
        <span>{message}</span>
      </div>
      <button
        onClick={() => onClose(id)}
        className='ml-4 text-gray-500 hover:text-gray-800'
        aria-label='Close'
      >
        ✕
      </button>
    </div>
  );
}

export function ToastContainer({
  toasts,
  onClose,
}: {
  toasts: ToastType[];
  onClose: (id: string) => void;
}) {
  if (toasts.length === 0) return null;

  return (
    <div className='fixed bottom-4 right-4 z-50 flex flex-col max-w-xs w-full'>
      {toasts.map((toast) => (
        <Toast key={toast.id} toast={toast} onClose={onClose} />
      ))}
    </div>
  );
}
