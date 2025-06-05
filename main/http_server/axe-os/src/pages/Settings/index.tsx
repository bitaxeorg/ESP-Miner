import { useState } from "preact/hooks";
import { ActionCard } from "../../components/ActionCard";
import { Button } from "../../components/Button";
import { Container } from "../../components/Container";
import { PageHeading } from "../../components/PageHeading";
import { updatePresetSettings } from "../../utils/api";
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
  const [loading, setLoading] = useState(false);

  const handleModeSelect = (mode: PresetMode) => {
    setSelectedMode(mode);
  };

  const handleSave = async () => {
    if (!selectedMode) {
      showToast("Please select a mode first", "error");
      return;
    }

    setLoading(true);

    try {
      const result = await updatePresetSettings(selectedMode);

      if (result.success) {
        showToast(result.message, "success");
      } else {
        showToast(result.message, "error");
      }
    } catch (error) {
      console.error("Failed to save preset settings:", error);
      showToast("Failed to save settings", "error");
    } finally {
      setLoading(false);
    }
  };

  return (
    <Container>
      <PageHeading
        title="Settings"
        subtitle="Configure your miner's performance and operational modes"
      />

      <ActionCard
        title='Choose Your Mode'
        description='Adjust your Touch for quiet operation, balanced performance, or maximum hashing power—just tap and save.'
      >
        <div className='space-y-6'>
          {/* Mode Selection Cards */}
          <div className='grid grid-cols-1 md:grid-cols-3 gap-4'>
            {modeCards.map((mode) => {
              const Icon = mode.icon;
              const isSelected = selectedMode === mode.id;

              return (
                <div
                  key={mode.id}
                  onClick={() => handleModeSelect(mode.id)}
                  className={`
                    relative p-4 rounded-lg border-2 cursor-pointer transition-all duration-200 hover:scale-105
                    ${
                      isSelected
                        ? "border-[var(--primary-color)] bg-[color-mix(in_srgb,var(--primary-color),transparent_90%)]"
                        : "border-[#1C2F52] bg-[var(--card-bg)] hover:border-[color-mix(in_srgb,var(--primary-color),transparent_50%)]"
                    }
                  `}
                >
                  <div className='flex flex-col items-center text-center space-y-3'>
                    <div
                      className={`
                      p-3 rounded-full transition-colors
                      ${
                        isSelected
                          ? "bg-[var(--primary-color)] text-[var(--primary-color-text)]"
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

                  {/* Selected indicator */}
                  {isSelected && (
                    <div className='absolute top-2 right-2'>
                      <div className='w-3 h-3 bg-[var(--primary-color)] rounded-full'></div>
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
              disabled={loading || !selectedMode}
              className='bg-blue-600 hover:bg-blue-700 px-8 py-3'
            >
              {loading ? "Saving..." : "Save Settings"}
            </Button>
          </div>
        </div>
      </ActionCard>
    </Container>
  );
}
