import { createContext, h } from "preact";
import { useContext, useEffect, useRef } from "preact/hooks";
import { useToast } from "./ToastContext";
import { SystemInfo } from "../utils/types/systemInfo";

interface OverheatWarningContextType {
  checkOverheatStatus: (systemInfo: SystemInfo) => void;
}

const OverheatWarningContext = createContext<OverheatWarningContextType>({
  checkOverheatStatus: () => {},
});

export function OverheatWarningProvider({ children }: { children: preact.ComponentChildren }) {
  const { showToast, hidePersistentToasts } = useToast();
  const currentOverheatStatus = useRef<boolean>(false);

  const checkOverheatStatus = (systemInfo: SystemInfo) => {
    const isOverheating = systemInfo.overheat_mode === 1;

    // Only update if status has changed
    if (isOverheating !== currentOverheatStatus.current) {
      currentOverheatStatus.current = isOverheating;

      if (isOverheating) {
        // Show persistent warning toast
        showToast(
          `⚠️ OVERHEAT MODE ACTIVE - Temperature: ${systemInfo.temp}°C. Mining performance may be reduced for hardware protection.`,
          "warning",
          true
        );
      } else {
        // Hide the overheat warning when no longer overheating
        hidePersistentToasts("warning");
      }
    }
  };

  return (
    <OverheatWarningContext.Provider value={{ checkOverheatStatus }}>
      {children}
    </OverheatWarningContext.Provider>
  );
}

export function useOverheatWarning() {
  return useContext(OverheatWarningContext);
}