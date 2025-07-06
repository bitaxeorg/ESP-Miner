import { useState, useEffect } from "preact/hooks";
import { Button } from "../../components/Button";
import { setSSID, getSystemInfo, scanWifiNetworks, WifiNetwork } from "../../utils/api";
import { logger } from "../../utils/logger";
import { useToast } from "../../context/ToastContext";
import { Container } from "../../components/Container";
import { PageHeading } from "../../components/PageHeading";

interface WifiFormData {
  ssid: string;
  wifiPass: string;
}

interface WifiStatus {
  isConnected: boolean;
  currentSSID: string;
  macAddr: string;
  hostname: string;
}

export function WifiPage() {
  const { showToast } = useToast();
  const [formData, setFormData] = useState<WifiFormData>({
    ssid: "",
    wifiPass: "",
  });
  const [loading, setLoading] = useState(false);
  const [scanning, setScanning] = useState(false);
  const [availableNetworks, setAvailableNetworks] = useState<WifiNetwork[]>([]);
  const [selectedNetworkType, setSelectedNetworkType] = useState<"available" | "other">("available");
  const [selectedSSID, setSelectedSSID] = useState<string>("");
  const [wifiStatus, setWifiStatus] = useState<WifiStatus>({
    isConnected: false,
    currentSSID: "",
    macAddr: "",
    hostname: "",
  });

  const handleScanNetworks = async () => {
    setScanning(true);
    try {
      const result = await scanWifiNetworks();
      if (result.success) {
        setAvailableNetworks(result.networks);
        // If we have networks, default to available networks mode
        if (result.networks.length > 0) {
          setSelectedNetworkType("available");
          setSelectedSSID(result.networks[0]?.ssid || "");
        } else {
          // If no networks found, switch to manual entry
          setSelectedNetworkType("other");
        }
      } else {
        showToast(result.message || "Failed to scan networks", "error");
        setSelectedNetworkType("other");
      }
    } catch (error) {
      logger.error("Failed to scan networks:", error);
      showToast("Failed to scan for networks", "error");
      setSelectedNetworkType("other");
    } finally {
      setScanning(false);
    }
  };

  // Fetch current WiFi status on component mount
  useEffect(() => {
    const fetchWifiStatus = async () => {
      try {
        const systemInfo = await getSystemInfo();
        setWifiStatus({
          isConnected: systemInfo.wifiStatus === "Connected!",
          currentSSID: systemInfo.ssid || "",
          macAddr: systemInfo.macAddr || "",
          hostname: systemInfo.hostname || "",
        });
      } catch (error) {
        logger.error("Failed to fetch WiFi status:", error);
      }
    };

    fetchWifiStatus();
    // Automatically scan for networks on page load
    handleScanNetworks();
  }, []);

  const refreshWifiStatus = async () => {
    try {
      const systemInfo = await getSystemInfo();
      setWifiStatus({
        isConnected: systemInfo.wifiStatus === "Connected!",
        currentSSID: systemInfo.ssid || "",
        macAddr: systemInfo.macAddr || "",
        hostname: systemInfo.hostname || "",
      });
    } catch (error) {
      logger.error("Failed to refresh WiFi status:", error);
    }
  };

  const handleInputChange = (field: keyof WifiFormData) => (e: Event) => {
    const target = e.target as HTMLInputElement;
    setFormData((prev) => ({
      ...prev,
      [field]: target.value,
    }));
  };

  const handleNetworkSelection = (e: Event) => {
    const target = e.target as HTMLSelectElement;
    const value = target.value;

    if (value === "other") {
      setSelectedNetworkType("other");
      setSelectedSSID("");
      setFormData(prev => ({ ...prev, ssid: "" }));
    } else {
      setSelectedNetworkType("available");
      setSelectedSSID(value);
      setFormData(prev => ({ ...prev, ssid: value }));
    }
  };

  const handleManualSSIDChange = (e: Event) => {
    const target = e.target as HTMLInputElement;
    setFormData(prev => ({ ...prev, ssid: target.value }));
  };

  const getAuthModeDescription = (authmode: number): string => {
    const authModes = [
      "Open", "WEP", "WPA", "WPA2", "WPA/WPA2", "WPA2 Enterprise",
      "WPA3", "WPA2/WPA3", "WAPI", "OWE", "WPA3 Enterprise", "WPA3", "WPA3"
    ];
    return authModes[authmode] || "Unknown";
  };

  const handleSubmit = async (e: Event) => {
    e.preventDefault();

    const ssidToUse = selectedNetworkType === "available" ? selectedSSID : formData.ssid;

    if (!ssidToUse.trim()) {
      showToast("SSID is required", "error");
      return;
    }

    if (!formData.wifiPass.trim()) {
      showToast("Password is required", "error");
      return;
    }

    setLoading(true);

    try {
      const result = await setSSID(ssidToUse.trim(), formData.wifiPass);

      if (result.success) {
        showToast(result.message, "success");
        // Reset form on success
        setFormData({ ssid: "", wifiPass: "" });
        setSelectedSSID("");
        await refreshWifiStatus();
      } else {
        showToast(result.message, "error");
      }
    } catch (error) {
      logger.error("Failed to set SSID:", error);
      showToast(error instanceof Error ? error.message : "Failed to configure Wi-Fi", "error");
    } finally {
      setLoading(false);
    }
  };

  return (
    <Container>
      <PageHeading
        title='Wi-Fi Settings'
        subtitle='Configure wireless network connection for your miner.'
        link='https://help.advancedcryptoservices.com/en/articles/11517746-wifi-settings'
      />

      <div className='bg-[var(--card-bg)] rounded-lg shadow-md max-w-full md:max-w-xl'>
        <form onSubmit={handleSubmit}>
          <div className='flex justify-between items-center mb-6'>
            <h2 className='text-xl font-medium'>Network Configuration</h2>
          </div>

          <div className='mb-6'>
            <div className='flex items-center gap-2 mb-4'>
              <div
                className={`w-2 h-2 rounded-full ${
                  wifiStatus.isConnected ? "bg-green-500" : "bg-red-500"
                }`}
              />
              <span className='text-[var(--text-secondary)]'>
                Status: {wifiStatus.isConnected ? "Connected" : "Offline"}
              </span>
            </div>

            {wifiStatus.isConnected && (
              <div className='grid grid-cols-1 md:grid-cols-3 gap-3 mb-4'>
                {wifiStatus.currentSSID && (
                  <div className='bg-[var(--input-bg)] border border-slate-700 rounded-md p-3 text-center'>
                    <div className='text-xs text-[var(--text-secondary)] mb-1'>Network</div>
                    <div className='text-sm font-medium truncate'>{wifiStatus.currentSSID}</div>
                  </div>
                )}
                {wifiStatus.macAddr && (
                  <div className='bg-[var(--input-bg)] border border-slate-700 rounded-md p-3 text-center'>
                    <div className='text-xs text-[var(--text-secondary)] mb-1'>MAC Address</div>
                    <div className='text-sm font-medium font-mono'>{wifiStatus.macAddr}</div>
                  </div>
                )}
                {wifiStatus.hostname && (
                  <div className='bg-[var(--input-bg)] border border-slate-700 rounded-md p-3 text-center'>
                    <div className='text-xs text-[var(--text-secondary)] mb-1'>Hostname</div>
                    <div className='text-sm font-medium'>{wifiStatus.hostname}</div>
                  </div>
                )}
              </div>
            )}
          </div>

          <div className='mb-6'>
            <div className='grid grid-cols-1 gap-4 border border-slate-700 rounded-md p-4'>
              <div>
                <div className='flex items-center justify-between mb-2'>
                  <label className='block text-sm font-medium text-left' htmlFor='network-select'>
                    Network Name (SSID)
                  </label>
                  <button
                    type="button"
                    onClick={handleScanNetworks}
                    disabled={scanning}
                    className='p-1 text-[var(--text-secondary)] hover:text-[var(--text-primary)] disabled:opacity-50 disabled:cursor-not-allowed'
                    title={scanning ? "Scanning..." : "Refresh Networks"}
                  >
                    <svg
                      className={`w-4 h-4 ${scanning ? 'animate-spin' : ''}`}
                      fill="none"
                      stroke="currentColor"
                      viewBox="0 0 24 24"
                    >
                      <path
                        strokeLinecap="round"
                        strokeLinejoin="round"
                        strokeWidth={2}
                        d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15"
                      />
                    </svg>
                  </button>
                </div>

                {availableNetworks.length > 0 ? (
                  <select
                    id='network-select'
                    value={selectedNetworkType === "available" ? selectedSSID : "other"}
                    onChange={handleNetworkSelection}
                    className='w-full p-2 pr-8 border border-slate-700 rounded-md bg-[var(--input-bg)] mb-2'
                    disabled={loading || scanning}
                  >
                    {availableNetworks.map((network) => (
                      <option key={network.ssid} value={network.ssid}>
                        {network.ssid} ({network.rssi} dBm, {getAuthModeDescription(network.authmode)})
                      </option>
                    ))}
                    <option value="other">Other (Enter manually)</option>
                  </select>
                ) : (
                  <div className='text-sm text-[var(--text-secondary)] mb-2 p-2 bg-[var(--input-bg)] border border-slate-700 rounded-md'>
                    {scanning ? "Scanning for networks..." : "No networks found. Enter SSID manually."}
                  </div>
                )}

                {(selectedNetworkType === "other" || availableNetworks.length === 0) && (
                  <input
                    type='text'
                    id='manual-ssid'
                    name='manual-ssid'
                    value={formData.ssid}
                    onChange={handleManualSSIDChange}
                    className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                    placeholder='Enter Wi-Fi network name'
                    required
                    disabled={loading}
                  />
                )}
              </div>

              <div>
                <label className='block text-sm font-medium mb-1 text-left' htmlFor='password'>
                  Password
                </label>
                <input
                  type='password'
                  id='password'
                  name='password'
                  value={formData.wifiPass}
                  onChange={handleInputChange("wifiPass")}
                  className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                  placeholder='Enter Wi-Fi password'
                  required
                  disabled={loading}
                />
              </div>

              <Button type='submit' disabled={loading || scanning} className='bg-blue-600 hover:bg-blue-700'>
                {loading ? "Configuring..." : "Connect to Network"}
              </Button>
            </div>
          </div>
        </form>
      </div>
    </Container>
  );
}
