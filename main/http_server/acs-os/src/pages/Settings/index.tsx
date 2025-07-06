import { useState, useEffect } from "preact/hooks";
import { ActionCard } from "../../components/ActionCard";
import { Button } from "../../components/Button";
import { Container } from "../../components/Container";
import { PageHeading } from "../../components/PageHeading";
import { updatePresetSettings, updateYouTubeUrl, validateYouTubeChannel, getSystemInfo, SystemInfo } from "../../utils/api";
import { logger } from "../../utils/logger";
import { useToast } from "../../context/ToastContext";
import { Volume2, BarChart3, Zap } from "lucide-preact";

type PresetMode = "quiet" | "balanced" | "turbo";

interface ModeCard {
  id: PresetMode;
  title: string;
  description: string;
  icon: any;
}

const modeCards: ModeCard[] = [
  {
    id: "quiet",
    title: "Quiet Mode",
    description: "Lower fan speed for silent operation—perfect for when sound matters most.",
    icon: Volume2,
  },
  {
    id: "balanced",
    title: "Balanced Mode",
    description: "A smart balance of performance and noise for everyday mining.",
    icon: BarChart3,
  },
  {
    id: "turbo",
    title: "Turbo Mode",
    description: "Maximized hash rate and speed for peak mining performance.",
    icon: Zap,
  },
];

export function Settings() {
  const { showToast } = useToast();
    const [selectedMode, setSelectedMode] = useState<PresetMode | null>(null);
  const [currentMode, setCurrentMode] = useState<PresetMode | null>(null);
  const [loading, setLoading] = useState(false);
  const [youtubeUrl, setYoutubeUrl] = useState("");
  const [youtubeLoading, setYoutubeLoading] = useState(false);
  const [validatedChannelId, setValidatedChannelId] = useState<string>("");
  const [fetchingInfo, setFetchingInfo] = useState(true);

  // Helper function to get the current preset from SystemInfo
  const getCurrentPreset = (info: SystemInfo): PresetMode | null => {
    // Support both current and future key names
    const presetValue = info.autotune_preset || (info as any).autotunePreset;
    if (presetValue && typeof presetValue === 'string') {
      const preset = presetValue.toLowerCase() as PresetMode;
      if (['quiet', 'balanced', 'turbo'].includes(preset)) {
        return preset;
      }
    }
    return null;
  };

  // Fetch system info on component mount
  useEffect(() => {
    const fetchSystemInfo = async () => {
      try {
        setFetchingInfo(true);
        const info = await getSystemInfo();

        const currentPreset = getCurrentPreset(info);
        setCurrentMode(currentPreset);
        setSelectedMode(currentPreset); // Initialize selected mode to current
      } catch (error) {
        logger.error("Failed to fetch system info:", error);
        showToast("Failed to load current settings", "error");
      } finally {
        setFetchingInfo(false);
      }
    };

    fetchSystemInfo();
  }, [showToast]);

  const handleModeSelect = (mode: PresetMode) => {
    setSelectedMode(mode);
  };

  const validateYouTubeUrl = (url: string): boolean => {
    if (!url.trim()) return false;

    // YouTube URL patterns
    const patterns = [
      /^https?:\/\/(www\.)?youtube\.com\/@[\w-]+$/,
      /^https?:\/\/(www\.)?youtube\.com\/channel\/[a-zA-Z0-9_-]+$/,
      /^youtube\.com\/channel\/[a-zA-Z0-9_-]+$/,
      /^(www\.)?youtube\.com\/@[\w-]+$/,
    ];

    return patterns.some(pattern => pattern.test(url));
  };

  const handleSave = async () => {
    if (!selectedMode) {
      showToast("Please select a mode first", "error");
      return;
    }

    setLoading(true);

    try {
      const result = await updatePresetSettings(selectedMode);

      if (result.status === "success") {
        showToast(result.message, "success");
        setCurrentMode(selectedMode); // Update current mode after successful save
      } else {
        showToast(result.message, "error");
      }
    } catch (error) {
      logger.error("Failed to save preset settings:", error);
      showToast("Failed to save settings", "error");
    } finally {
      setLoading(false);
    }
  };

  const handleYouTubeSave = async () => {
    if (!youtubeUrl.trim()) {
      showToast("Please enter a YouTube URL", "error");
      return;
    }

    if (!validateYouTubeUrl(youtubeUrl)) {
      showToast("Please enter a valid YouTube channel URL", "error");
      return;
    }

    setYoutubeLoading(true);

    try {
      // First validate the channel exists
      showToast("Validating YouTube channel...", "info");
      const validation = await validateYouTubeChannel(youtubeUrl);

      if (!validation.isValid) {
        showToast(validation.error || "Channel not found", "error");
        return;
      }

      // Store the validated channel ID for later use
      if (validation.channelInfo?.id) {
        setValidatedChannelId(validation.channelInfo.id);
      }

      // If validation passes, save the URL to ESP32
      const result = await updateYouTubeUrl(youtubeUrl);

      if (result.success) {
        const channelName = validation.channelInfo?.title || "channel";
        showToast(`Successfully saved ${channelName}!`, "success");
      } else {
        showToast(result.message, "error");
      }
    } catch (error) {
      console.error("Failed to save YouTube URL:", error);
      showToast("Failed to save YouTube URL", "error");
    } finally {
      setYoutubeLoading(false);
    }
  };

  return (
    <Container>
      <PageHeading
        title='Settings'
        subtitle="Configure your miner's performance and operational modes."
        link='https://help.advancedcryptoservices.com/en/articles/11517773-settings'
      />

      <div className="border border-slate-600 rounded-lg p-6 bg-slate-900/20">
        <ActionCard
          title='Choose Your Mode'
          description='Adjust your device for quiet operation, balanced performance, or maximum hashing power—just tap and save.'
        >
          <div className='space-y-6'>
            {/* Current Setting Display */}
            {!fetchingInfo && currentMode && (
              <div className="mb-6 p-4 bg-blue-500/10 border border-blue-500/20 rounded-lg">
                <div className="flex items-center gap-3">
                  <div className="w-2 h-2 bg-blue-500 rounded-full" />
                  <span className="text-blue-100 text-sm">
                    Currently using: <span className="font-semibold text-white">
                      {currentMode.charAt(0).toUpperCase() + currentMode.slice(1)} Mode
                    </span>
                  </span>
                </div>
              </div>
            )}

            {/* Mode Selection Cards */}
            <div className='grid grid-cols-1 md:grid-cols-3 gap-4'>
              {modeCards.map((mode) => {
                const Icon = mode.icon;
                const isSelected = selectedMode === mode.id;
                const isCurrent = currentMode === mode.id;

                return (
                  <div
                    key={mode.id}
                    onClick={() => handleModeSelect(mode.id)}
                    className={`
                      relative p-4 rounded-md border cursor-pointer transition-all duration-200
                      ${
                        isSelected
                          ? "border-blue-500 bg-blue-500/10"
                          : "border-slate-700 bg-[var(--card-bg)] hover:border-slate-500 hover:bg-slate-800/30"
                      }
                    `}
                  >
                    <div className='flex flex-col items-center text-center space-y-3'>
                      <div
                        className={`
                        p-3 rounded-full transition-colors
                        ${
                          isSelected
                            ? "bg-blue-500 text-white"
                            : "bg-[#1C2F52] text-[#8B96A5]"
                        }
                      `}
                      >
                        <Icon className='h-6 w-6' />
                      </div>

                      <h3
                        className={`
                        font-medium text-lg
                        ${isSelected ? "text-white" : "text-white"}
                      `}
                      >
                        {mode.title}
                      </h3>

                      <p
                        className={`
                        text-sm leading-relaxed
                        ${isSelected ? "text-[#E2E8F0]" : "text-[#8B96A5]"}
                      `}
                      >
                        {mode.description}
                      </p>
                    </div>

                    {/* Current mode indicator */}
                    {isCurrent && (
                      <div className='absolute top-2 left-2'>
                        <div className='flex items-center gap-1'>
                          <div className='w-2 h-2 bg-green-500 rounded-full' />
                          <span className='text-xs text-green-400 font-medium'>Current</span>
                        </div>
                      </div>
                    )}

                    {/* Selected indicator */}
                    {isSelected && (
                      <div className='absolute top-2 right-2'>
                        <div className='w-3 h-3 bg-blue-500 rounded-full' />
                      </div>
                    )}
                  </div>
                );
              })}
            </div>

            {/* Save Button */}
            <div className='flex justify-start pt-4'>
              <Button
                onClick={handleSave}
                disabled={loading || !selectedMode || fetchingInfo}
                className='bg-blue-600 hover:bg-blue-700 px-8 py-3'
              >
                {loading ? "Saving..." : fetchingInfo ? "Loading..." : "Save Settings"}
              </Button>
            </div>
          </div>
        </ActionCard>
      </div>

      {/* <div className="mt-8 border border-slate-600 rounded-lg p-6 bg-slate-900/20">
        <ActionCard
          title='YouTube Channel'
          description='Set your preferred YouTube channel to display  stats on the Touch.'
        >
          <div className='space-y-4'>
            <div>
              <label
                className='block text-sm font-medium mb-2 text-left text-white'
                htmlFor='youtubeUrl'
              >
                YouTube Channel URL
              </label>
              <input
                type='text'
                id='youtubeUrl'
                name='youtubeUrl'
                value={youtubeUrl}
                onChange={(e) => setYoutubeUrl((e.target as HTMLInputElement).value)}
                className='w-full p-3 border border-slate-700 rounded-md bg-[var(--input-bg)] text-white placeholder-slate-400'
                placeholder='https://www.youtube.com/@handle or youtube.com/channel/XXXXXXXXXX'
              />
              <p className='text-xs text-slate-400 mt-1'>
                Supported formats: @username or channel/ID URLs
              </p>
            </div>
            <div className='flex justify-start pt-2'>
              <Button
                onClick={handleYouTubeSave}
                disabled={youtubeLoading || !youtubeUrl.trim()}
                className='bg-blue-600 hover:bg-blue-700 px-8 py-3'
              >
                {youtubeLoading ? "Saving..." : "Save YouTube URL"}
              </Button>
            </div>
          </div>
        </ActionCard>
      </div> */}
    </Container>
  );
}
