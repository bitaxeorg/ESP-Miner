import { useState, useEffect } from "preact/hooks";
import { Button } from "../../components/Button";
import { getSystemInfo, updatePoolInfo, restartSystem } from "../../utils/api";
import { useToast } from "../../context/ToastContext";

export function Settings() {
  const { showToast } = useToast();
  const [formData, setFormData] = useState({
    stratumURL: "",
    stratumPort: "",
  });
  const [loading, setLoading] = useState(false);
  const [restartLoading, setRestartLoading] = useState(false);

  useEffect(() => {
    // Load current settings
    const loadSettings = async () => {
      try {
        const systemInfo = await getSystemInfo();
        setFormData({
          stratumURL: systemInfo.stratumURL || "",
          stratumPort: systemInfo.stratumPort?.toString() || "",
        });
      } catch (error) {
        console.error("Failed to load settings:", error);
        showToast("Failed to load current settings", "error");
      }
    };

    loadSettings();
  }, [showToast]);

  const handleInputChange = (e: Event) => {
    const target = e.target as HTMLInputElement;
    setFormData({
      ...formData,
      [target.name]: target.value,
    });
  };

  const handleSubmit = async (e: Event) => {
    e.preventDefault();
    setLoading(true);

    try {
      const portNumber = parseInt(formData.stratumPort, 10);

      if (isNaN(portNumber)) {
        throw new Error("Port must be a valid number");
      }

      await updatePoolInfo(formData.stratumURL, portNumber);
      showToast("Pool settings updated successfully", "success");
    } catch (error) {
      console.error("Failed to update pool settings:", error);
      showToast(error instanceof Error ? error.message : "Failed to update pool settings", "error");
    } finally {
      setLoading(false);
    }
  };

  const handleRestart = async () => {
    setRestartLoading(true);

    try {
      await restartSystem();
      showToast("System is restarting...", "info");
    } catch (error) {
      console.error("Failed to restart system:", error);
      showToast(error instanceof Error ? error.message : "Failed to restart system", "error");
    } finally {
      setRestartLoading(false);
    }
  };

  return (
    <div className='p-6'>
      <h1 className='text-2xl font-bold mb-6'>Pool Settings</h1>

      <div className='max-w-md bg-[var(--card-bg)] p-6 rounded-lg shadow-md'>
        <form onSubmit={handleSubmit}>
          <div className='mb-4'>
            <label className='block text-sm font-medium mb-1' htmlFor='stratumURL'>
              Stratum URL
            </label>
            <input
              type='text'
              id='stratumURL'
              name='stratumURL'
              value={formData.stratumURL}
              onChange={handleInputChange}
              className='w-full p-2 border border-[var(--border-color)] rounded-md bg-[var(--input-bg)] text-[var(--text-color)]'
              placeholder='e.g., solo.ckpool.org'
              required
            />
          </div>

          <div className='mb-6'>
            <label className='block text-sm font-medium mb-1' htmlFor='stratumPort'>
              Stratum Port
            </label>
            <input
              type='text'
              id='stratumPort'
              name='stratumPort'
              value={formData.stratumPort}
              onChange={handleInputChange}
              className='w-full p-2 border border-[var(--border-color)] rounded-md bg-[var(--input-bg)] text-[var(--text-color)]'
              placeholder='e.g., 3333'
              required
            />
          </div>

          <div className='flex gap-4'>
            <Button type='submit' disabled={loading} className='flex-1'>
              {loading ? "Saving..." : "Save Settings"}
            </Button>

            <Button
              type='button'
              variant='outline'
              disabled={restartLoading}
              onClick={handleRestart}
              className='flex-1'
            >
              {restartLoading ? "Restarting..." : "Restart Device"}
            </Button>
          </div>
        </form>
      </div>
    </div>
  );
}
