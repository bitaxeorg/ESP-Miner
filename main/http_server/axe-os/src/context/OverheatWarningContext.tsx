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
          <div>
            ⚠️ OVERHEAT PROTECTION TRIGGERED! Frequency and voltage have been reset to default values.{" "}
            <a
              href="https://help.advancedcryptoservices.com/en/articles/11510751-overheat-protection"
              target="_blank"
              rel="noopener noreferrer"
              className="text-blue-600 hover:text-blue-800 underline"
            >
              Learn more
            </a>
          </div>,
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