import preactLogo from "../../assets/preact.svg";
import { useState, useEffect } from "preact/hooks";
import { getSystemInfo, SystemInfo } from "../../utils/api";
import { formatUptime } from "../../utils/formatters";
import { DataSection } from "../../components/DataSection";
import { Tabs } from "../../components/Tabs";
import {
  Activity,
  AudioLines,
  Battery,
  ChevronDown,
  CpuIcon,
  Flag,
  Gauge,
  Home,
  Layers,
  PanelLeft,
  Pickaxe,
  Settings,
  Thermometer,
  ThumbsDown,
  ThumbsUp,
  Users,
  Zap,
} from "lucide-preact";

export function HomePage() {
  const [systemInfo, setSystemInfo] = useState<SystemInfo | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [activeTab, setActiveTab] = useState("overview");

  const tabs = [
    { id: "overview", label: "Overview" },
    { id: "power", label: "Power" },
    { id: "thermals", label: "Thermals" },
    { id: "system", label: "System" },
  ];

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
      value: systemInfo?.hashRate ? `${(systemInfo.hashRate / 1000).toFixed(2)} TH/s` : null,
      icon: Activity,
    },
    {
      title: "Shares Accepted",
      value: systemInfo?.sharesAccepted?.toLocaleString() || "0",
      icon: ThumbsUp,
      color: "green",
    },
    {
      title: "Shares Rejected",
      value: systemInfo?.sharesRejected?.toLocaleString() || "0",
      icon: ThumbsDown,
      color: "red",
    },
  ];

  // Power & Thermal
  const powerThermalCards = [
    {
      title: "Frequency",
      value: systemInfo?.frequency ? `${systemInfo.frequency} MHz` : null,
      icon: AudioLines,
    },
    {
      title: "Voltage",
      value: systemInfo?.voltage ? `${(systemInfo.voltage / 1000).toFixed(2)}V` : null,
      icon: Zap,
    },
    {
      title: "Power",
      value: systemInfo?.power ? `${systemInfo.power.toFixed(2)}W` : null,
      icon: Battery,
    },
  ];

  // Core Operation
  const coreOperationCards = [
    {
      title: "Core Voltage Set",
      value: systemInfo?.coreVoltage ? `${systemInfo.coreVoltage} mV` : null,
      icon: AudioLines,
    },
    {
      title: "Core Voltage Actual",
      value: systemInfo?.coreVoltageActual ? `${systemInfo.coreVoltageActual} mV` : null,
      icon: Battery,
    },
  ];

  // System Info
  const systemInfoCards = [
    {
      title: "ASIC Model",
      value: systemInfo?.ASICModel || null,
      icon: CpuIcon,
    },
    {
      title: "ASIC Count",
      value: systemInfo?.asicCount || null,
      icon: Layers,
    },
    {
      title: "Small Core Count",
      value: systemInfo?.smallCoreCount?.toLocaleString() || null,
      icon: CpuIcon,
    },
  ];

  // Runtime & Memory
  const runtimeMemoryCards = [
    {
      title: "Uptime",
      value: systemInfo?.uptimeSeconds ? formatUptime(systemInfo.uptimeSeconds) : null,
      icon: Activity,
    },
    {
      title: "Free Heap",
      value: systemInfo?.freeHeap ? `${(systemInfo.freeHeap / 1024 / 1024).toFixed(2)} MB` : null,
      icon: Layers,
    },
    {
      title: "PSRAM Available",
      value: systemInfo?.isPSRAMAvailable ? "Yes" : "No",
      icon: CpuIcon,
    },
  ];

  // Versioning
  const versioningCards = [
    {
      title: "Firmware Version",
      value: systemInfo?.version || null,
      className: "text-base",
      icon: Settings,
    },
    {
      title: "IDF Version",
      value: systemInfo?.idfVersion || null,
      className: "text-base",
      icon: Settings,
    },
    {
      title: "Board Version",
      value: systemInfo?.boardVersion || null,
      icon: Settings,
    },
  ];

  // VR & Fan
  const vrFanCards = [
    {
      title: "Temperature",
      value: systemInfo?.temp ? `${systemInfo.temp.toFixed(1)}°C` : null,
      icon: Thermometer,
    },
    {
      title: "VR Temp",
      value: systemInfo?.vrTemp ? `${systemInfo.vrTemp}°C` : null,
      icon: Thermometer,
    },
    {
      title: "Fan Speed",
      value: systemInfo?.fanspeed ? `${systemInfo.fanspeed}%` : null,
      icon: Activity,
    },
    {
      title: "Fan RPM",
      value: systemInfo?.fanrpm?.toLocaleString() || null,
      icon: Activity,
    },
  ];

  // Misc Flags
  const miscFlagsCards = [
    {
      title: "Flip Screen",
      value: systemInfo?.flipscreen ? "Yes" : "No",
      icon: PanelLeft,
    },
    {
      title: "Invert Screen",
      value: systemInfo?.invertscreen ? "Yes" : "No",
      icon: PanelLeft,
    },
    {
      title: "Overheat Mode",
      value: systemInfo?.overheat_mode ? "On" : "Off",
      icon: Thermometer,
    },
  ];

  // Other Settings
  const otherSettingsCards = [
    {
      title: "Stratum Diff",
      value: systemInfo?.stratumDiff?.toLocaleString() || null,
      icon: Pickaxe,
    },
    {
      title: "Best Diff",
      value: systemInfo?.bestDiff || null,
      icon: Pickaxe,
    },
    {
      title: "Session Best Diff",
      value: systemInfo?.bestSessionDiff || null,
      icon: Pickaxe,
    },
  ];

  return (
    <div class='space-y-6'>
      <Tabs tabs={tabs} activeTab={activeTab} onTabChange={setActiveTab} />
      {activeTab === "overview" && (
        <>
          <DataSection
            title='Performance Metrics'
            cards={performanceCards}
            loading={loading && !systemInfo}
            error={error}
            icon={Gauge}
          />

          <DataSection
            title='Difficulty Metrics'
            cards={otherSettingsCards}
            loading={loading && !systemInfo}
            error={error}
            icon={Pickaxe}
          />
        </>
      )}
      {activeTab === "power" && (
        <>
          <DataSection
            title='Power'
            cards={powerThermalCards}
            loading={loading && !systemInfo}
            error={error}
            icon={Zap}
          />

          <DataSection
            title='Core Operation'
            cards={coreOperationCards}
            loading={loading && !systemInfo}
            error={error}
            icon={CpuIcon}
          />
        </>
      )}
      {activeTab === "thermals" && (
        <>
          <DataSection
            title='VR & Fan'
            cards={vrFanCards}
            loading={loading && !systemInfo}
            error={error}
            icon={Thermometer}
          />
        </>
      )}
      {activeTab === "system" && (
        <>
          <DataSection
            title='System Info'
            cards={systemInfoCards}
            loading={loading && !systemInfo}
            error={error}
            icon={Layers}
          />

          <DataSection
            title='Runtime & Memory'
            cards={runtimeMemoryCards}
            loading={loading && !systemInfo}
            error={error}
            icon={CpuIcon}
          />

          <DataSection
            title='Versioning'
            cards={versioningCards}
            loading={loading && !systemInfo}
            error={error}
            icon={CpuIcon}
          />

          <DataSection
            title='Misc Flags'
            cards={miscFlagsCards}
            loading={loading && !systemInfo}
            error={error}
            icon={Flag}
          />
        </>
      )}
      {activeTab === "btc" && (
        <div class='text-slate-400 text-center py-8'>BTC information coming soon...</div>
      )}
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
