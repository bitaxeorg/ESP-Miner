import { createContext, h } from "preact";
import { useState, useContext, useCallback } from "preact/hooks";

export type ToastType = "success" | "error" | "info" | "warning";

export interface Toast {
  id: string;
  message: string;
  type: ToastType;
  persistent?: boolean;
}

interface ToastContextType {
  toasts: Toast[];
  showToast: (message: string, type?: ToastType, persistent?: boolean) => void;
  hideToast: (id: string) => void;
  hidePersistentToasts: (type?: ToastType) => void;
}

const ToastContext = createContext<ToastContextType>({
  toasts: [],
  showToast: () => {},
  hideToast: () => {},
  hidePersistentToasts: () => {},
});

export function ToastProvider({ children }: { children: preact.ComponentChildren }) {
  const [toasts, setToasts] = useState<Toast[]>([]);

  const showToast = useCallback((message: string, type: ToastType = "info", persistent: boolean = false) => {
    const id = Date.now().toString();
    const newToast = { id, message, type, persistent };

    // If it's a persistent warning, remove any existing persistent warnings of the same type
    if (persistent && type === "warning") {
      setToasts((prev) => prev.filter((toast) => !(toast.persistent && toast.type === "warning")));
    }

    setToasts((prev) => [...prev, newToast]);

    // Auto-remove toast after 3 seconds only if not persistent
    if (!persistent) {
      setTimeout(() => {
        hideToast(id);
      }, 3000);
    }
  }, []);

  const hideToast = useCallback((id: string) => {
    setToasts((prev) => prev.filter((toast) => toast.id !== id));
  }, []);

  const hidePersistentToasts = useCallback((type?: ToastType) => {
    setToasts((prev) => prev.filter((toast) => {
      if (!toast.persistent) return true;
      if (type && toast.type !== type) return true;
      return false;
    }));
  }, []);

  return (
    <ToastContext.Provider value={{ toasts, showToast, hideToast, hidePersistentToasts }}>
      {children}
    </ToastContext.Provider>
  );
}

export function useToast() {
  return useContext(ToastContext);
}
