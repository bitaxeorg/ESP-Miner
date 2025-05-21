import { useState, useEffect } from "preact/hooks";
import { Button } from "../../components/Button";
import { getSystemInfo, updatePoolInfo, restartSystem } from "../../utils/api";
import { useToast } from "../../context/ToastContext";
import { Container } from "../../components/Container";

const POOL_OPTIONS = ["public-pool.io", "solo.ckpool.org", "Other"];

export function PoolsPage() {
  const { showToast } = useToast();
  const [formData, setFormData] = useState({
    primaryPool: {
      url: "",
      port: "",
      password: "",
      btcAddress: "",
      workerName: "",
    },
    fallbackPool: {
      url: "",
      port: "",
      password: "",
      btcAddress: "",
      workerName: "",
    },
    stratumOption: "public-pool.io",
    fallbackOption: "",
  });
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    // Load current settings
    const loadSettings = async () => {
      try {
        const systemInfo = await getSystemInfo();
        const savedURL = systemInfo.stratumURL || "";

        // Determine if the saved URL matches one of our predefined options
        const matchedOption =
          POOL_OPTIONS.find((option) => option !== "Other" && option === savedURL) || "Other";

        setFormData({
          primaryPool: {
            url: savedURL,
            port: systemInfo.stratumPort?.toString() || "",
            password: systemInfo.stratumPassword || "",
            btcAddress: systemInfo.stratumUser || "",
            workerName: systemInfo.stratumWorkerName || "",
          },
          fallbackPool: {
            url: systemInfo.fallbackStratumURL || "",
            port: systemInfo.fallbackStratumPort?.toString() || "",
            password: systemInfo.fallbackStratumPassword || "",
            btcAddress: systemInfo.fallbackStratumUser || "",
            workerName: systemInfo.fallbackStratumWorkerName || "",
          },
          stratumOption: matchedOption,
          fallbackOption: systemInfo.fallbackStratumURL ? "Other" : "",
        });
      } catch (error) {
        console.error("Failed to load settings:", error);
        showToast("Failed to load current settings", "error");
      }
    };

    loadSettings();
  }, [showToast]);

  const handleInputChange = (poolType, field) => (e: Event) => {
    const target = e.target as HTMLInputElement;
    setFormData((prev) => ({
      ...prev,
      [poolType]: {
        ...prev[poolType],
        [field]: target.value,
      },
    }));
  };

  const handleStratumOptionChange = (e: Event) => {
    const target = e.target as HTMLSelectElement;
    const option = target.value;

    setFormData((prev) => ({
      ...prev,
      stratumOption: option,
      primaryPool: {
        ...prev.primaryPool,
        url: option !== "Other" ? option : prev.primaryPool.url,
      },
    }));
  };

  const handleFallbackOptionChange = (e: Event) => {
    const target = e.target as HTMLSelectElement;
    const option = target.value;

    setFormData((prev) => ({
      ...prev,
      fallbackOption: option,
      fallbackPool: {
        ...prev.fallbackPool,
        url: option !== "Other" ? option : prev.fallbackPool.url,
      },
    }));
  };

  const handleSubmitAndRestart = async (e: Event) => {
    e.preventDefault();
    setLoading(true);

    try {
      // For "Other" option, port is required
      if (formData.stratumOption === "Other") {
        const primaryPort = parseInt(formData.primaryPool.port, 10);
        if (isNaN(primaryPort)) {
          throw new Error("Primary port must be a valid number");
        }
      } else {
        // For predefined pools, BTC address is required
        if (!formData.primaryPool.btcAddress.trim()) {
          throw new Error("BTC payout address is required");
        }
      }

      let fallbackPort = null;
      if (formData.fallbackOption === "Other" && formData.fallbackPool.port) {
        fallbackPort = parseInt(formData.fallbackPool.port, 10);
        if (isNaN(fallbackPort)) {
          throw new Error("Fallback port must be a valid number");
        }
      } else if (formData.fallbackOption && formData.fallbackOption !== "Other") {
        // For predefined fallback pools, BTC address is required
        if (!formData.fallbackPool.btcAddress.trim()) {
          throw new Error("Fallback BTC payout address is required");
        }
      }

      // Use either the selected option or the custom URL
      const primaryURL =
        formData.stratumOption !== "Other" ? formData.stratumOption : formData.primaryPool.url;

      const fallbackURL =
        formData.fallbackOption && formData.fallbackOption !== "Other"
          ? formData.fallbackOption
          : formData.fallbackPool.url;

      // First save the settings
      await updatePoolInfo(
        primaryURL,
        formData.stratumOption === "Other"
          ? parseInt(formData.primaryPool.port, 10)
          : primaryURL === "public-pool.io"
          ? 21496
          : 3333, // Default ports for known pools
        formData.primaryPool.password,
        formData.primaryPool.btcAddress, // BTC payout address
        fallbackURL || undefined,
        formData.fallbackOption === "Other" && fallbackPort
          ? fallbackPort
          : fallbackURL === "public-pool.io"
          ? 21496
          : fallbackURL === "solo.ckpool.org"
          ? 3333
          : null,
        formData.fallbackPool.password || undefined,
        formData.fallbackPool.btcAddress || undefined, // Fallback BTC payout address
        formData.primaryPool.workerName || undefined, // Worker name for primary pool
        formData.fallbackPool.workerName || undefined // Worker name for fallback pool
      );
      showToast("Pool settings updated successfully", "success");

      // Then restart the system
      await restartSystem();
      showToast("System is restarting...", "info");
    } catch (error) {
      console.error("Failed to update settings or restart:", error);
      showToast(
        error instanceof Error ? error.message : "Failed to update settings or restart",
        "error"
      );
    } finally {
      setLoading(false);
    }
  };

  const handleAddUrl = () => {
    // For now, this just focuses on the fallback URL field
    // In a more complex implementation, this could add multiple pool configurations
    const fallbackUrlElement = document.getElementById("fallbackOption");
    if (fallbackUrlElement) {
      fallbackUrlElement.focus();
    }

    // Initialize fallback option if not already set
    if (!formData.fallbackOption) {
      setFormData((prev) => ({
        ...prev,
        fallbackOption: "public-pool.io",
        fallbackPool: {
          ...prev.fallbackPool,
          url: "public-pool.io",
        },
      }));
    }
  };

  return (
    <Container>
      <div className='flex justify-between items-center mb-6'>
        <h1 className='text-2xl font-bold'>Pool Settings</h1>
      </div>

      <div className='bg-[var(--card-bg)] p-6 rounded-lg shadow-md max-w-3xl mx-auto'>
        <form onSubmit={handleSubmitAndRestart}>
          <div className='flex justify-between items-center mb-6'>
            <h2 className='text-xl font-medium'>Stratum URLs</h2>
          </div>

          <div className='mb-8'>
            <div className='grid grid-cols-1 gap-4 mb-4 border border-slate-700 rounded-md p-4'>
              <div className='grid grid-cols-1 md:grid-cols-2 gap-4'>
                <div>
                  <label
                    className='block text-sm font-medium mb-1 text-left'
                    htmlFor='stratumOption'
                  >
                    Stratum URL
                  </label>
                  <select
                    id='stratumOption'
                    name='stratumOption'
                    value={formData.stratumOption}
                    onChange={handleStratumOptionChange}
                    className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                    required
                  >
                    {POOL_OPTIONS.map((option) => (
                      <option key={option} value={option}>
                        {option}
                      </option>
                    ))}
                  </select>
                  {formData.stratumOption === "Other" && (
                    <input
                      type='text'
                      name='primaryUrl'
                      value={formData.primaryPool.url}
                      onChange={handleInputChange("primaryPool", "url")}
                      className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)] mt-2'
                      placeholder='e.g., mypool.example.com'
                      required
                    />
                  )}
                </div>
                <div>
                  {formData.stratumOption === "Other" ? (
                    <>
                      <label
                        className='block text-sm font-medium mb-1 text-left'
                        htmlFor='primaryPort'
                      >
                        Stratum Port
                      </label>
                      <input
                        type='text'
                        id='primaryPort'
                        name='primaryPort'
                        value={formData.primaryPool.port}
                        onChange={handleInputChange("primaryPool", "port")}
                        className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                        placeholder='e.g., 3333'
                        required
                      />
                    </>
                  ) : (
                    <>
                      <label
                        className='block text-sm font-medium mb-1 text-left'
                        htmlFor='primaryBtcAddress'
                      >
                        BTC Payout Address
                      </label>
                      <input
                        type='text'
                        id='primaryBtcAddress'
                        name='primaryBtcAddress'
                        value={formData.primaryPool.btcAddress}
                        onChange={handleInputChange("primaryPool", "btcAddress")}
                        className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                        placeholder='Enter your BTC address'
                        required
                      />
                    </>
                  )}
                </div>
              </div>

              {formData.stratumOption === "Other" && (
                <div>
                  <label
                    className='block text-sm font-medium mb-1 text-left'
                    htmlFor='primaryWorkerName'
                  >
                    Worker Name
                  </label>
                  <input
                    type='text'
                    id='primaryWorkerName'
                    name='primaryWorkerName'
                    value={formData.primaryPool.workerName}
                    onChange={handleInputChange("primaryPool", "workerName")}
                    className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                    placeholder='Enter worker name'
                  />
                </div>
              )}

              <div>
                <label
                  className='block text-sm font-medium mb-1 text-left'
                  htmlFor='primaryPassword'
                >
                  Password
                </label>
                <input
                  type='text'
                  id='primaryPassword'
                  name='primaryPassword'
                  value={formData.primaryPool.password}
                  onChange={handleInputChange("primaryPool", "password")}
                  className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                  placeholder='Enter password (optional)'
                />
              </div>
            </div>
          </div>

          {/* Fallback Stratum URL Section */}
          <div className='mb-8'>
            <h2 className='text-xl font-medium mb-4'>Fallback Stratum URL</h2>
            <div className='grid grid-cols-1 gap-4 mb-4 border border-slate-700 rounded-md p-4'>
              <div className='grid grid-cols-1 md:grid-cols-2 gap-4'>
                <div>
                  <label
                    className='block text-sm font-medium mb-1 text-left'
                    htmlFor='fallbackOption'
                  >
                    Fallback URL
                  </label>
                  <select
                    id='fallbackOption'
                    name='fallbackOption'
                    value={formData.fallbackOption}
                    onChange={handleFallbackOptionChange}
                    className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                  >
                    <option value=''>Select a fallback pool</option>
                    {POOL_OPTIONS.map((option) => (
                      <option key={option} value={option}>
                        {option}
                      </option>
                    ))}
                  </select>
                  {formData.fallbackOption === "Other" && (
                    <input
                      type='text'
                      name='fallbackUrl'
                      value={formData.fallbackPool.url}
                      onChange={handleInputChange("fallbackPool", "url")}
                      className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)] mt-2'
                      placeholder='e.g., fallback.example.com'
                    />
                  )}
                </div>
                <div>
                  {formData.fallbackOption &&
                    (formData.fallbackOption === "Other" ? (
                      <>
                        <label
                          className='block text-sm font-medium mb-1 text-left'
                          htmlFor='fallbackPort'
                        >
                          Fallback Port
                        </label>
                        <input
                          type='text'
                          id='fallbackPort'
                          name='fallbackPort'
                          value={formData.fallbackPool.port}
                          onChange={handleInputChange("fallbackPool", "port")}
                          className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                          placeholder='Enter port number'
                        />
                      </>
                    ) : (
                      <>
                        <label
                          className='block text-sm font-medium mb-1 text-left'
                          htmlFor='fallbackBtcAddress'
                        >
                          Fallback BTC Payout Address
                        </label>
                        <input
                          type='text'
                          id='fallbackBtcAddress'
                          name='fallbackBtcAddress'
                          value={formData.fallbackPool.btcAddress}
                          onChange={handleInputChange("fallbackPool", "btcAddress")}
                          className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                          placeholder='Enter your BTC address'
                          required={!!formData.fallbackOption}
                        />
                      </>
                    ))}
                </div>
              </div>

              {formData.fallbackOption === "Other" && (
                <div>
                  <label
                    className='block text-sm font-medium mb-1 text-left'
                    htmlFor='fallbackWorkerName'
                  >
                    Worker Name
                  </label>
                  <input
                    type='text'
                    id='fallbackWorkerName'
                    name='fallbackWorkerName'
                    value={formData.fallbackPool.workerName}
                    onChange={handleInputChange("fallbackPool", "workerName")}
                    className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                    placeholder='Enter worker name'
                  />
                </div>
              )}

              <div>
                <label
                  className='block text-sm font-medium mb-1 text-left'
                  htmlFor='fallbackPassword'
                >
                  Fallback Password
                </label>
                <input
                  type='text'
                  id='fallbackPassword'
                  name='fallbackPassword'
                  value={formData.fallbackPool.password}
                  onChange={handleInputChange("fallbackPool", "password")}
                  className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                  placeholder='Enter password (optional)'
                />
              </div>
            </div>
          </div>

          <Button type='submit' disabled={loading} className='w-full bg-red-600 hover:bg-red-700'>
            {loading ? "Processing..." : "Save & Restart Device"}
          </Button>
        </form>
      </div>
    </Container>
  );
}
