import { useState, useEffect } from "preact/hooks";
import { Button } from "../../components/Button";
import { setSSID, getSystemInfo } from "../../utils/api";
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
  const [wifiStatus, setWifiStatus] = useState<WifiStatus>({
    isConnected: false,
    currentSSID: "",
    macAddr: "",
    hostname: "",
  });

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
        console.error("Failed to fetch WiFi status:", error);
      }
    };

    fetchWifiStatus();
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
      console.error("Failed to refresh WiFi status:", error);
    }
  };

  const handleInputChange = (field: keyof WifiFormData) => (e: Event) => {
    const target = e.target as HTMLInputElement;
    setFormData((prev) => ({
      ...prev,
      [field]: target.value,
    }));
  };

  const handleSubmit = async (e: Event) => {
    e.preventDefault();

    if (!formData.ssid.trim()) {
      showToast("SSID is required", "error");
      return;
    }

    if (!formData.wifiPass.trim()) {
      showToast("Password is required", "error");
      return;
    }

    setLoading(true);

    try {
      const result = await setSSID(formData.ssid.trim(), formData.wifiPass);

      if (result.success) {
        showToast(result.message, "success");
        // Reset form on success
        setFormData({ ssid: "", wifiPass: "" });
        await refreshWifiStatus();
      } else {
        showToast(result.message, "error");
      }
    } catch (error) {
      console.error("Failed to set SSID:", error);
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
                  wifiStatus.isConnected ? 'bg-green-500' : 'bg-red-500'
                }`}
              />
              <span className='text-[var(--text-secondary)]'>
                Status: {wifiStatus.isConnected ? 'Connected' : 'Offline'}
              </span>
            </div>

            {wifiStatus.isConnected && (
              <div className='grid grid-cols-3 gap-3 mb-4'>
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
                <label className='block text-sm font-medium mb-1 text-left' htmlFor='ssid'>
                  Network Name (SSID)
                </label>
                <input
                  type='text'
                  id='ssid'
                  name='ssid'
                  value={formData.ssid}
                  onChange={handleInputChange("ssid")}
                  className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                  placeholder='Enter Wi-Fi network name'
                  required
                  disabled={loading}
                />
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

              <Button type='submit' disabled={loading} className='bg-blue-600 hover:bg-blue-700'>
                {loading ? "Configuring..." : "Connect to Network"}
              </Button>
            </div>
          </div>
        </form>
      </div>
    </Container>
  );
}
