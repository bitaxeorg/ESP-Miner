import { useState, useEffect } from "preact/hooks";
import { Button } from "../../components/Button";
import { getSystemInfo, updatePoolInfo, restartSystem } from "../../utils/api";
import { useToast } from "../../context/ToastContext";
import { Container } from "../../components/Container";
import { PageHeading } from "../../components/PageHeading";

const POOL_OPTIONS = ["public-pool.io", "solo.ckpool.org", "Other"];

export function PoolsPage() {
  const { showToast } = useToast();
  const [formData, setFormData] = useState({
    primaryPool: {
      url: "",
      port: "",
      password: "",
      user: "", // BTC payout address for predefined pools
      accountName: "", // Account name for custom pools
      workerName: "", // Worker name for custom pools
    },
    fallbackPool: {
      url: "",
      port: "",
      password: "",
      user: "", // BTC payout address for predefined pools
      accountName: "", // Account name for custom pools
      workerName: "", // Worker name for custom pools
    },
    stratumOption: "public-pool.io", // Selected pool option: predefined pool URLs or "Other"
    fallbackOption: "", // Selected fallback pool option: predefined pool URLs, "Other", or empty
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

                // Parse existing saved data to properly populate form on load
        const parseExistingUserData = (userField: string, isCustomPool: boolean) => {
          if (isCustomPool && userField && userField.includes('.')) {
            const [accountName, ...workerParts] = userField.split('.');
            return {
              accountName: accountName || "",
              workerName: workerParts.join('.') || "ACSBitaxeTouch",
              user: "",
            };
          }
          return {
            accountName: "",
            workerName: isCustomPool ? "ACSBitaxeTouch" : "",
            user: userField || "",
          };
        };

        const getPrimaryPoolData = () => {
          const primaryUserData = parseExistingUserData(systemInfo.stratumUser || "", matchedOption === "Other");

          if (matchedOption === "Other") {
            // Custom pools - load existing data or defaults
            return {
              url: savedURL,
              port: systemInfo.stratumPort?.toString() || "",
              password: systemInfo.stratumPassword || "",
              user: primaryUserData.user,
              accountName: primaryUserData.accountName,
              workerName: primaryUserData.workerName,
            };
          } else {
            // Predefined pools use existing data
            return {
              url: savedURL,
              port: systemInfo.stratumPort?.toString() || "",
              password: systemInfo.stratumPassword || "",
              user: primaryUserData.user,
              accountName: primaryUserData.accountName,
              workerName: primaryUserData.workerName,
            };
          }
        };

                const getFallbackPoolData = () => {
          const fallbackMatchedOption = systemInfo.fallbackStratumURL
            ? POOL_OPTIONS.find((option) => option !== "Other" && option === systemInfo.fallbackStratumURL) || "Other"
            : "";

          const fallbackUserData = parseExistingUserData(systemInfo.fallbackStratumUser || "", fallbackMatchedOption === "Other");

          if (fallbackMatchedOption === "Other") {
            // Custom fallback pools - load existing data or defaults
            return {
              url: systemInfo.fallbackStratumURL || "",
              port: systemInfo.fallbackStratumPort?.toString() || "",
              password: systemInfo.fallbackStratumPassword || "",
              user: fallbackUserData.user,
              accountName: fallbackUserData.accountName,
              workerName: fallbackUserData.workerName,
              option: fallbackMatchedOption,
            };
          } else {
            // Predefined fallback pools use existing data
            return {
              url: systemInfo.fallbackStratumURL || "",
              port: systemInfo.fallbackStratumPort?.toString() || "",
              password: systemInfo.fallbackStratumPassword || "",
              user: fallbackUserData.user,
              accountName: fallbackUserData.accountName,
              workerName: fallbackUserData.workerName,
              option: fallbackMatchedOption,
            };
          }
        };

        const primaryPoolData = getPrimaryPoolData();
        const fallbackPoolData = getFallbackPoolData();

        setFormData({
          primaryPool: {
            url: primaryPoolData.url,
            port: primaryPoolData.port,
            password: primaryPoolData.password,
            user: primaryPoolData.user,
            accountName: primaryPoolData.accountName,
            workerName: primaryPoolData.workerName,
          },
          fallbackPool: {
            url: fallbackPoolData.url,
            port: fallbackPoolData.port,
            password: fallbackPoolData.password,
            user: fallbackPoolData.user,
            accountName: fallbackPoolData.accountName,
            workerName: fallbackPoolData.workerName,
          },
          stratumOption: matchedOption,
          fallbackOption: fallbackPoolData.option,
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
        url: option !== "Other" ? option : "",
        // Clear custom pool fields when switching to "Other"
        accountName: option === "Other" ? "" : prev.primaryPool.accountName,
        workerName: option === "Other" ? "ACSBitaxeTouch" : prev.primaryPool.workerName,
        port: option === "Other" ? "" : prev.primaryPool.port,
        password: option === "Other" ? "" : prev.primaryPool.password,
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
        url: option !== "Other" ? option : "",
        // Clear custom pool fields when switching to "Other"
        accountName: option === "Other" ? "" : prev.fallbackPool.accountName,
        workerName: option === "Other" ? "ACSBitaxeTouch" : prev.fallbackPool.workerName,
        port: option === "Other" ? "" : prev.fallbackPool.port,
        password: option === "Other" ? "" : prev.fallbackPool.password,
      },
    }));
  };

  const handleSubmitAndRestart = async (e: Event) => {
    e.preventDefault();
    setLoading(true);

    try {
      // Validate primary pool settings
      if (formData.stratumOption === "Other") {
        // For custom pools, URL, port, account name and worker name are required
        if (!formData.primaryPool.url.trim()) {
          throw new Error("Stratum URL is required for custom pools");
        }
        if (!formData.primaryPool.port.trim()) {
          throw new Error("Port is required for custom pools");
        }
        const primaryPort = parseInt(formData.primaryPool.port, 10);
        if (isNaN(primaryPort)) {
          throw new Error("Primary port must be a valid number");
        }
        if (!formData.primaryPool.accountName.trim()) {
          throw new Error("Account name is required for custom pools");
        }
        if (!formData.primaryPool.workerName.trim()) {
          throw new Error("Worker name is required for custom pools");
        }
      } else {
        // For predefined pools, BTC payout address is required
        if (!formData.primaryPool.user.trim()) {
          throw new Error("BTC payout address is required for predefined pools");
        }
      }

      // Validate fallback pool settings
      let fallbackPort = null;
      if (formData.fallbackOption === "Other") {
        // URL, port, account name and worker name are required for custom fallback pools
        if (!formData.fallbackPool.url.trim()) {
          throw new Error("Fallback stratum URL is required for custom pools");
        }
        if (!formData.fallbackPool.port.trim()) {
          throw new Error("Fallback port is required for custom pools");
        }
        fallbackPort = parseInt(formData.fallbackPool.port, 10);
        if (isNaN(fallbackPort)) {
          throw new Error("Fallback port must be a valid number");
        }
        if (!formData.fallbackPool.accountName.trim()) {
          throw new Error("Fallback account name is required for custom pools");
        }
        if (!formData.fallbackPool.workerName.trim()) {
          throw new Error("Fallback worker name is required for custom pools");
        }
      } else if (formData.fallbackOption && formData.fallbackOption !== "Other") {
        // For predefined fallback pools, BTC address is required
        if (!formData.fallbackPool.user.trim()) {
          throw new Error("BTC payout address is required for predefined fallback pools");
        }
      }

      // Use either the selected option or the custom URL
      const primaryURL =
        formData.stratumOption !== "Other" ? formData.stratumOption : formData.primaryPool.url;

      const fallbackURL =
        formData.fallbackOption && formData.fallbackOption !== "Other"
          ? formData.fallbackOption
          : formData.fallbackPool.url;

      // Prepare stratum user fields
      const primaryStratumUser = formData.stratumOption === "Other"
        ? `${formData.primaryPool.accountName}.${formData.primaryPool.workerName}`
        : formData.primaryPool.user;

      const fallbackStratumUser = formData.fallbackOption === "Other"
        ? `${formData.fallbackPool.accountName}.${formData.fallbackPool.workerName}`
        : formData.fallbackPool.user;

      // First save the settings
      await updatePoolInfo(
        primaryURL,
        formData.stratumOption === "Other"
          ? parseInt(formData.primaryPool.port, 10)
          : primaryURL === "public-pool.io"
          ? 21496
          : 3333, // Default ports for known pools
        formData.primaryPool.password,
        primaryStratumUser, // BTC address for predefined pools, accountName.workerName for custom pools
        fallbackURL || undefined,
        formData.fallbackOption === "Other" && fallbackPort
          ? fallbackPort
          : fallbackURL === "public-pool.io"
          ? 21496
          : fallbackURL === "solo.ckpool.org"
          ? 3333
          : null,
        formData.fallbackPool.password || undefined,
        fallbackStratumUser || undefined // BTC address for predefined pools, accountName.workerName for custom pools
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

  return (
    <Container>
      <PageHeading
        title='Pool Settings'
        subtitle='Configure mining pools and stratum connections.'
        link='https://help.advancedcryptoservices.com/en/articles/11517348-mining-pools'
      />

      <div className='bg-[var(--card-bg)] rounded-lg shadow-md max-w-full md:max-w-3xl'>
        <form onSubmit={handleSubmitAndRestart}>
          <div className='flex justify-between items-center mb-4'>
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
                    <div className='mt-2'>
                      <input
                        type='text'
                        name='primaryUrl'
                        value={formData.primaryPool.url}
                        onChange={handleInputChange("primaryPool", "url")}
                        className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                        placeholder='btc.global.luxor.tech'
                        required
                      />
                      <p className='text-sm text-gray-400 mt-1'>
                        Do not include "stratum+tcp://" prefix or port number. Enter only the
                        hostname.
                      </p>
                    </div>
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
                        value={formData.primaryPool.user}
                        onChange={handleInputChange("primaryPool", "user")}
                        className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                        placeholder='Enter your BTC payout address'
                        required
                      />
                    </>
                  )}
                </div>
              </div>

              {formData.stratumOption === "Other" && (
                <div className='grid grid-cols-1 md:grid-cols-2 gap-4'>
                  <div>
                    <label
                      className='block text-sm font-medium mb-1 text-left'
                      htmlFor='primaryAccountName'
                    >
                      Account Name
                    </label>
                    <input
                      type='text'
                      id='primaryAccountName'
                      name='primaryAccountName'
                      value={formData.primaryPool.accountName}
                      onChange={handleInputChange("primaryPool", "accountName")}
                      className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                      placeholder='Enter account name'
                      required
                    />
                  </div>
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
                      required
                    />
                  </div>
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
                    <div className='mt-2'>
                      <input
                        type='text'
                        name='fallbackUrl'
                        value={formData.fallbackPool.url}
                        onChange={handleInputChange("fallbackPool", "url")}
                        className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                        placeholder='btc.global.luxor.tech'
                        required
                      />
                      <p className='text-sm text-gray-400 mt-1'>
                        Do not include "stratum+tcp://" prefix or port number. Enter only the
                        hostname.
                      </p>
                    </div>
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
                          placeholder='e.g., 3333'
                          required
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
                          value={formData.fallbackPool.user}
                          onChange={handleInputChange("fallbackPool", "user")}
                          className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                          placeholder='Enter your BTC payout address'
                          required={!!formData.fallbackOption}
                        />
                      </>
                    ))}
                </div>
              </div>

              {formData.fallbackOption === "Other" && (
                <div className='grid grid-cols-1 md:grid-cols-2 gap-4'>
                  <div>
                    <label
                      className='block text-sm font-medium mb-1 text-left'
                      htmlFor='fallbackSubAccount'
                    >
                      Account Name
                    </label>
                                          <input
                        type='text'
                        id='fallbackAccountName'
                        name='fallbackAccountName'
                        value={formData.fallbackPool.accountName}
                        onChange={handleInputChange("fallbackPool", "accountName")}
                        className='w-full p-2 border border-slate-700 rounded-md bg-[var(--input-bg)]'
                        placeholder='Enter account name'
                        required
                      />
                  </div>
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
                      required
                    />
                  </div>
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

          <Button type='submit' disabled={loading} className='bg-blue-600 hover:bg-blue-700'>
            {loading ? "Processing..." : "Save & Restart Device"}
          </Button>
        </form>
      </div>
    </Container>
  );
}
