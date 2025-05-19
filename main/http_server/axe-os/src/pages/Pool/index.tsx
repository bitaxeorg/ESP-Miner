import { useState, useEffect } from "preact/hooks";
import { getSystemInfo, SystemInfo } from "../../utils/api";
import { DataSection } from "../../components/DataSection";
import { Network, Settings } from "lucide-preact";

export function PoolPage() {
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

  return (
    <div class='space-y-6'>
      <DataSection
        title='Network Info'
        cards={networkInfoCards}
        loading={loading && !systemInfo}
        error={error}
        icon={Network}
      />

      <DataSection
        title='Stratum Settings'
        cards={stratumSettingsCards}
        loading={loading && !systemInfo}
        error={error}
        icon={Settings}
      />

      <DataSection
        title='Fallback Stratum'
        cards={fallbackStratumCards}
        loading={loading && !systemInfo}
        error={error}
        icon={Settings}
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
