import { useState, useEffect, useRef } from "preact/hooks";
import { Button } from "../../components/Button";
import { Switch } from "../../components/Switch";
import { Slider } from "../../components/Slider";
import { ActionCard } from "../../components/ActionCard";
import { fetchMiners, SystemInfo, updateFanSettings, updateASICSettings } from "../../utils/api";
import { useToast } from "../../context/ToastContext";
import { Container } from "../../components/Container";

export function SettingsAdvanced() {
  const { showToast } = useToast();
  const [isUpdating, setIsUpdating] = useState(false);
  const [isUpdatingASIC, setIsUpdatingASIC] = useState(false);
  const [minerInfo, setMinerInfo] = useState<SystemInfo | null>(null);
  const [isLoading, setIsLoading] = useState(true);
  // const fileInputRef = useRef<HTMLInputElement>(null);

  const fetchSystemInfo = async () => {
    try {
      setIsLoading(true);
      const miners = await fetchMiners();
      // Use the first miner (current implementation only returns one)
      setMinerInfo(miners.length > 0 ? miners[0] : null);
    } catch (error) {
      showToast(
        `Error fetching system info: ${error instanceof Error ? error.message : "Unknown error"}`,
        "error"
      );
    } finally {
      setIsLoading(false);
    }
  };

  const [isEnabled, setIsEnabled] = useState(false);
  const [fanSpeed, setFanSpeed] = useState(0);
  const [frequency, setFrequency] = useState(0);
  const [voltage, setVoltage] = useState(0);

  // Track original values to detect changes
  const [originalIsEnabled, setOriginalIsEnabled] = useState(false);
  const [originalFanSpeed, setOriginalFanSpeed] = useState(0);
  const [originalFrequency, setOriginalFrequency] = useState(0);
  const [originalVoltage, setOriginalVoltage] = useState(0);

  const onCheckedChange = (checked: boolean) => {
    setIsEnabled(checked);
  };

  // Check if fan settings have changed
  const hasChanges =
    isEnabled !== originalIsEnabled || (!isEnabled && fanSpeed !== originalFanSpeed);

  // Check if ASIC settings have changed
  const hasASICChanges = frequency !== originalFrequency || voltage !== originalVoltage;

  const handleSaveFanSettings = async () => {
    // Show confirmation dialog
    const confirmMessage = `Are you sure you want to save these fan settings?\n\nAutomatic fan control: ${
      isEnabled ? "Enabled" : "Disabled"
    }${!isEnabled ? `\nFan speed: ${fanSpeed}%` : ""}`;

    if (!confirm(confirmMessage)) {
      return;
    }

    try {
      setIsUpdating(true);

      const autofanspeed = isEnabled ? 1 : 0;
      const result = await updateFanSettings(autofanspeed, !isEnabled ? fanSpeed : undefined);

      if (result.success) {
        showToast(result.message, "success");
        // Update original values to reflect the saved state
        setOriginalIsEnabled(isEnabled);
        setOriginalFanSpeed(fanSpeed);
        // Refresh system info to get latest values
        await fetchSystemInfo();
      } else {
        showToast(`Failed to save fan settings: ${result.message}`, "error");
      }
    } catch (error) {
      showToast(
        `Error saving fan settings: ${error instanceof Error ? error.message : "Unknown error"}`,
        "error"
      );
    } finally {
      setIsUpdating(false);
    }
  };

  const handleSaveASICSettings = async () => {
    // Show confirmation dialog
    const confirmMessage = `Are you sure you want to save these ASIC settings?\n\nFrequency: ${frequency} MHz\nVoltage: ${voltage} mV`;

    if (!confirm(confirmMessage)) {
      return;
    }

    try {
      setIsUpdatingASIC(true);

      const result = await updateASICSettings(voltage, frequency);

      if (result.success) {
        showToast(result.message, "success");
        // Update original values to reflect the saved state
        setOriginalFrequency(frequency);
        setOriginalVoltage(voltage);
        // Refresh system info to get latest values
        await fetchSystemInfo();
      } else {
        showToast(`Failed to save ASIC settings: ${result.message}`, "error");
      }
    } catch (error) {
      showToast(
        `Error saving ASIC settings: ${error instanceof Error ? error.message : "Unknown error"}`,
        "error"
      );
    } finally {
      setIsUpdatingASIC(false);
    }
  };

  useEffect(() => {
    fetchSystemInfo();
  }, [showToast]);

  // Update states when minerInfo changes
  useEffect(() => {
    if (minerInfo) {
      const autoFanEnabled = minerInfo.autofanspeed === 1;
      const currentFanSpeed = minerInfo.fanspeed || 0;

      setIsEnabled(autoFanEnabled);
      setFanSpeed(currentFanSpeed);
      setFrequency(minerInfo.frequency || 0);
      setVoltage(minerInfo.coreVoltage || 0);

      // Set original values
      setOriginalIsEnabled(autoFanEnabled);
      setOriginalFanSpeed(currentFanSpeed);
      setOriginalFrequency(minerInfo.frequency || 0);
      setOriginalVoltage(minerInfo.coreVoltage || 0);
    }
  }, [minerInfo]);

  return (
    <>
      <Container>
        <div className='flex items-center justify-between mb-6'>
          <h2 className='text-xl font-bold text-white'>Fan Settings</h2>
          <Button
            onClick={handleSaveFanSettings}
            disabled={!hasChanges || isUpdating}
            className='bg-blue-600 hover:bg-blue-700'
          >
            {isUpdating ? "Saving..." : "Save"}
          </Button>
        </div>
        <div className='grid grid-cols-1 gap-6 max-w-4xl mx-auto'>
          <ActionCard
            title='Automatic Fan Control'
            description={"Enable or disable automatic fan control"}
            link={""}
          >
            <div className='space-y-4'>
              <div className='flex flex-wrap gap-3'>
                <Switch
                  checked={isEnabled}
                  onCheckedChange={onCheckedChange}
                  aria-label='Enable automatic fan control'
                />
              </div>
              <div className='mt-4'>
                <div className='flex items-center gap-3'>
                  <div className='flex-1'></div>
                </div>
              </div>
            </div>
          </ActionCard>
          <div className='bg-[var(--card-bg)] rounded-xl border border-[#1C2F52] p-6 shadow-md'>
            <div className='flex items-start gap-4 mb-4'>
              <div className='min-w-0'>
                <h3 className='text-lg font-medium text-white mb-2'>Fan Speed Control</h3>
                <p className='text-[#8B96A5]'>
                  {isEnabled
                    ? "Disabled when automatic fan control is enabled"
                    : "Set the fan speed in percentage"}
                </p>
              </div>
              {!isEnabled && fanSpeed < 18 && (
                <div
                  className='bg-[#2A2D3A] border-l-4 border-yellow-500 text-white p-3 flex-1 self-stretch flex items-start gap-3 min-w-0'
                  role='alert'
                >
                  <div className='text-yellow-500 text-xl'>⚠</div>
                  <div className='flex-1'>
                    <p className='font-semibold text-yellow-400 mb-1'>Danger</p>
                    <p className='text-[#8B96A5] '>Could cause overheating</p>
                  </div>
                </div>
              )}
            </div>

            <div className='space-y-4'>
              <div className='flex flex-wrap gap-3'>
                <Slider
                  min={0}
                  max={100}
                  step={5}
                  value={[fanSpeed]}
                  onValueChange={(value) => setFanSpeed(value[0])}
                  disabled={isEnabled}
                />
              </div>
              <div className={`text-sm font-medium ${isEnabled ? "text-[#8B96A5]" : "text-white"}`}>
                {fanSpeed}%
              </div>
              <div className='mt-4'>
                <div className='flex items-center gap-3'>
                  <div className='flex-1'></div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </Container>
      <Container className='mt-10'>
        <div className='flex items-center justify-between mb-6'>
          <h2 className='text-xl font-bold text-white'>ASIC Settings</h2>
          <Button
            onClick={handleSaveASICSettings}
            disabled={!hasASICChanges || isUpdatingASIC}
            className='bg-blue-600 hover:bg-blue-700'
          >
            {isUpdatingASIC ? "Saving..." : "Save"}
          </Button>
        </div>
        <div className='grid grid-cols-1 gap-6 max-w-4xl mx-auto'>
          <ActionCard title='Frequency' description={"Set the frequency of the ASIC"} link={""}>
            <div className='space-y-4'>
              <div className='flex flex-wrap gap-3'>
                <Slider
                  min={200}
                  max={800}
                  step={25}
                  value={[frequency]}
                  onValueChange={(value) => setFrequency(value[0])}
                />
              </div>
              <div className='text-white text-sm font-medium'>{frequency} MHz</div>
              <div className='mt-4'>
                <div className='flex items-center gap-3'>
                  <div className='flex-1'></div>
                </div>
              </div>
            </div>
          </ActionCard>
          <ActionCard title='Voltage' description={"Set the voltage of the ASIC"} link={""}>
            <div className='space-y-4'>
              <div className='flex flex-wrap gap-3'>
                <Slider
                  min={500}
                  max={1200}
                  step={25}
                  value={[voltage]}
                  onValueChange={(value) => setVoltage(value[0])}
                />
              </div>
              <div className='text-white text-sm font-medium'>{voltage} mV</div>
              <div className='mt-4'>
                <div className='flex items-center gap-3'>
                  <div className='flex-1'></div>
                </div>
              </div>
            </div>
          </ActionCard>
        </div>
      </Container>
    </>
  );
}
