import preactLogo from "../../assets/preact.svg";
import { useState, useEffect } from "preact/hooks";
import { getSystemInfo, SystemInfo } from "../../utils/api";
import { formatUptime } from "../../utils/formatters";
import { DataSection } from "../../components/DataSection";

export function Home() {
  const [systemInfo, setSystemInfo] = useState<SystemInfo | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const fetchData = async () => {
      try {
        setLoading(true);
        const data = await getSystemInfo();
        setSystemInfo(data);
        setError(null);
      } catch (err) {
        setError("Failed to load system information");
        console.error(err);
      } finally {
        setLoading(false);
      }
    };

    fetchData();

    // Refresh data every 10 seconds
    const intervalId = setInterval(fetchData, 10000);

    return () => clearInterval(intervalId);
  }, []);

  // Calculate efficiency (W/T) if possible
  const efficiency =
    systemInfo && systemInfo.power && systemInfo.hashRate
      ? (systemInfo.power / (systemInfo.hashRate / 1000)).toFixed(2)
      : null;

  // Performance Metrics
  const performanceCards = [
    {
      title: "Hashrate",
      value: systemInfo?.hashRate ? `${(systemInfo.hashRate / 1000).toFixed(2)} GH/s` : null,
    },
    {
      title: "Shares Accepted",
      value: systemInfo?.sharesAccepted?.toLocaleString() || "0",
    },
    {
      title: "Shares Rejected",
      value: systemInfo?.sharesRejected?.toLocaleString() || "0",
    },
  ];

  // Power & Thermal
  const powerThermalCards = [
    {
      title: "Power",
      value: systemInfo?.power ? `${systemInfo.power.toFixed(2)}W` : null,
    },
    {
      title: "Voltage",
      value: systemInfo?.voltage ? `${(systemInfo.voltage / 1000).toFixed(2)}V` : null,
    },
    {
      title: "Temperature",
      value: systemInfo?.temp ? `${systemInfo.temp.toFixed(1)}°C` : null,
    },
  ];

  // Core Operation
  const coreOperationCards = [
    {
      title: "Core Voltage Set",
      value: systemInfo?.coreVoltage ? `${systemInfo.coreVoltage} mV` : null,
    },
    {
      title: "Core Voltage Actual",
      value: systemInfo?.coreVoltageActual ? `${systemInfo.coreVoltageActual} mV` : null,
    },
    {
      title: "Frequency",
      value: systemInfo?.frequency ? `${systemInfo.frequency} MHz` : null,
    },
  ];

  // Network Info
  const networkInfoCards = [
    {
      title: "SSID",
      value: systemInfo?.ssid || null,
    },
    {
      title: "MAC Address",
      value: systemInfo?.macAddr || null,
      className: "text-xl",
    },
    {
      title: "WiFi Status",
      value: systemInfo?.wifiStatus || null,
    },
  ];

  // Stratum Settings
  const stratumSettingsCards = [
    {
      title: "Stratum URL",
      value: systemInfo?.stratumURL || null,
    },
    {
      title: "Stratum Port",
      value: systemInfo?.stratumPort || null,
    },
    {
      title: "Stratum User",
      value: systemInfo?.stratumUser ? truncateAddress(systemInfo.stratumUser) : null,
      className: "text-lg",
    },
  ];

  // Fallback Stratum
  const fallbackStratumCards = [
    {
      title: "Fallback URL",
      value: systemInfo?.fallbackStratumURL || null,
    },
    {
      title: "Fallback Port",
      value: systemInfo?.fallbackStratumPort || null,
    },
    {
      title: "Fallback User",
      value: systemInfo?.fallbackStratumUser
        ? truncateAddress(systemInfo.fallbackStratumUser)
        : null,
      className: "text-lg",
    },
  ];

  // System Info
  const systemInfoCards = [
    {
      title: "ASIC Model",
      value: systemInfo?.ASICModel || null,
    },
    {
      title: "ASIC Count",
      value: systemInfo?.asicCount || null,
    },
    {
      title: "Small Core Count",
      value: systemInfo?.smallCoreCount?.toLocaleString() || null,
    },
  ];

  // Runtime & Memory
  const runtimeMemoryCards = [
    {
      title: "Uptime",
      value: systemInfo?.uptimeSeconds ? formatUptime(systemInfo.uptimeSeconds) : null,
    },
    {
      title: "Free Heap",
      value: systemInfo?.freeHeap ? `${(systemInfo.freeHeap / 1024 / 1024).toFixed(2)} MB` : null,
    },
    {
      title: "PSRAM Available",
      value: systemInfo?.isPSRAMAvailable ? "Yes" : "No",
    },
  ];

  // Versioning
  const versioningCards = [
    {
      title: "Firmware Version",
      value: systemInfo?.version || null,
      className: "text-base",
    },
    {
      title: "IDF Version",
      value: systemInfo?.idfVersion || null,
      className: "text-base",
    },
    {
      title: "Board Version",
      value: systemInfo?.boardVersion || null,
    },
  ];

  // VR & Fan
  const vrFanCards = [
    {
      title: "VR Temp",
      value: systemInfo?.vrTemp ? `${systemInfo.vrTemp}°C` : null,
    },
    {
      title: "Fan Speed",
      value: systemInfo?.fanspeed ? `${systemInfo.fanspeed}%` : null,
    },
    {
      title: "Fan RPM",
      value: systemInfo?.fanrpm?.toLocaleString() || null,
    },
  ];

  // Misc Flags
  const miscFlagsCards = [
    {
      title: "Flip Screen",
      value: systemInfo?.flipscreen ? "Yes" : "No",
    },
    {
      title: "Invert Screen",
      value: systemInfo?.invertscreen ? "Yes" : "No",
    },
    {
      title: "Overheat Mode",
      value: systemInfo?.overheat_mode ? "On" : "Off",
    },
  ];

  // Other Settings
  const otherSettingsCards = [
    {
      title: "Stratum Diff",
      value: systemInfo?.stratumDiff?.toLocaleString() || null,
    },
    {
      title: "Best Diff",
      value: systemInfo?.bestDiff || null,
    },
    {
      title: "Session Best Diff",
      value: systemInfo?.bestSessionDiff || null,
    },
  ];

  return (
    <div class='space-y-6'>
      <DataSection
        title='⚡ Performance Metrics'
        cards={performanceCards}
        loading={loading && !systemInfo}
        error={error}
      />

      <DataSection
        title='🔋 Power & Thermal'
        cards={powerThermalCards}
        loading={loading && !systemInfo}
        error={error}
      />

      <DataSection
        title='🔌 Core Operation'
        cards={coreOperationCards}
        loading={loading && !systemInfo}
        error={error}
      />

      <DataSection
        title='📶 Network Info'
        cards={networkInfoCards}
        loading={loading && !systemInfo}
        error={error}
      />

      <DataSection
        title='🌐 Stratum Settings'
        cards={stratumSettingsCards}
        loading={loading && !systemInfo}
        error={error}
      />

      <DataSection
        title='🧊 Fallback Stratum'
        cards={fallbackStratumCards}
        loading={loading && !systemInfo}
        error={error}
      />

      <DataSection
        title='🧠 System Info'
        cards={systemInfoCards}
        loading={loading && !systemInfo}
        error={error}
      />

      <DataSection
        title='💻 Runtime & Memory'
        cards={runtimeMemoryCards}
        loading={loading && !systemInfo}
        error={error}
      />

      <DataSection
        title='🧾 Versioning'
        cards={versioningCards}
        loading={loading && !systemInfo}
        error={error}
      />

      <DataSection
        title='🌡️ VR & Fan'
        cards={vrFanCards}
        loading={loading && !systemInfo}
        error={error}
      />

      <DataSection
        title='🧩 Misc Flags'
        cards={miscFlagsCards}
        loading={loading && !systemInfo}
        error={error}
      />

      <DataSection
        title='🧪 Other Settings'
        cards={otherSettingsCards}
        loading={loading && !systemInfo}
        error={error}
      />
    </div>
  );
}

/**
 * Truncates a long address string (e.g., wallet addresses)
 */
function truncateAddress(address: string): string {
  if (!address) return "";

  const parts = address.split(".");
  if (parts.length > 1) {
    // Handle "address.identifier" format
    const addr = parts[0];
    const identifier = parts[1];
    if (addr.length > 8) {
      return `${addr.substring(0, 5)}...${identifier}`;
    }
    return address;
  }

  // Regular address
  if (address.length > 12) {
    return `${address.substring(0, 6)}...${address.substring(address.length - 6)}`;
  }

  return address;
}
