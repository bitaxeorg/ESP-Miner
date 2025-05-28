import { useState } from "preact/hooks";
import { Button } from "../../components/Button";
import { setSSID } from "../../utils/api";
import { useToast } from "../../context/ToastContext";
import { Container } from "../../components/Container";

interface WifiFormData {
  ssid: string;
  wifiPass: string;
}

export function WifiPage() {
  const { showToast } = useToast();
  const [formData, setFormData] = useState<WifiFormData>({
    ssid: "",
    wifiPass: "",
  });
  const [loading, setLoading] = useState(false);

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

    if (!formData.password.trim()) {
      showToast("Password is required", "error");
      return;
    }

    setLoading(true);

    try {
      const result = await setSSID(formData.ssid.trim(), formData.password);

      if (result.success) {
        showToast(result.message, "success");
        // Reset form on success
        setFormData({ ssid: "", password: "" });
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
      <div className='flex justify-between items-center mb-6'>
        <h1 className='text-2xl font-bold'>Wi-Fi Settings</h1>
      </div>

      <div className='bg-[var(--card-bg)] p-4 md:p-6 rounded-lg shadow-md max-w-full md:max-w-2xl'>
        <form onSubmit={handleSubmit}>
          <div className='flex justify-between items-center mb-6'>
            <h2 className='text-xl font-medium'>Network Configuration</h2>
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
                  value={formData.password}
                  onChange={handleInputChange("password")}
                  className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                  placeholder='Enter Wi-Fi password'
                  required
                  disabled={loading}
                />
              </div>
            </div>
          </div>

          <Button type='submit' disabled={loading}>
            {loading ? "Configuring..." : "Connect to Network"}
          </Button>
        </form>
      </div>
    </Container>
  );
}
