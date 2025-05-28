import { useState, useEffect, useRef } from "preact/hooks";
import { Button } from "../../components/Button";
import { Switch } from "../../components/Switch";
import { Slider } from "../../components/Slider";
import {
  fetchMiners,
  uploadFirmware,
  uploadWebApp,
  FIRMWARE_LATEST_URL,
  WEBAPP_LATEST_URL,
  SystemInfo,
} from "../../utils/api";
import { useToast } from "../../context/ToastContext";
import { Container } from "../../components/Container";

interface ActionCardProps {
  title: string;
  description: string;
  buttonText?: string;
  onAction?: () => void;
  secondaryButtonText?: string;
  onSecondaryAction?: () => void;
  link?: string;
  children?: preact.ComponentChildren;
}

function ActionCard({
  title,
  description,
  buttonText,
  onAction,
  secondaryButtonText,
  onSecondaryAction,
  link,
  children,
}: ActionCardProps) {
  return (
    <div className='bg-[var(--card-bg)] rounded-xl border border-[#1C2F52] p-6 shadow-md'>
      <h3 className='text-lg font-medium text-white mb-2'>{title}</h3>
      <p className='text-[#8B96A5] mb-4'>{description}</p>

      {children || (
        <div className='flex flex-wrap gap-3'>
          {buttonText && onAction && <Button onClick={onAction}>{buttonText}</Button>}
          {link && (
            <Button as='a' href={link} download='esp-miner.bin'>
              Download Firmware
            </Button>
          )}
          {secondaryButtonText && onSecondaryAction && (
            <Button variant='outline' onClick={onSecondaryAction}>
              {secondaryButtonText}
            </Button>
          )}
        </div>
      )}
    </div>
  );
}

export function Settings() {
  const { showToast } = useToast();
  const [isUpdating, setIsUpdating] = useState(false);
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

  const onCheckedChange = (checked: boolean) => {
    setIsEnabled(checked);
  };

  useEffect(() => {
    fetchSystemInfo();
  }, [showToast]);

  return (
    <Container>
      <h2 className='text-xl font-bold text-white mb-6'>Fan Settings</h2>
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
        <ActionCard
          title='Fan Speed Control'
          description={"Set the fan speed in percentage"}
          link={""}
        >
          <div className='space-y-4'>
            <div className='flex flex-wrap gap-3'>
              <Slider
                min={0}
                max={100}
                step={5}
                value={[fanSpeed]}
                onValueChange={(value) => setFanSpeed(value[0])}
              />
            </div>
            <div className='mt-4'>
              <div className='flex items-center gap-3'>
                <div className='flex-1'></div>
              </div>
            </div>
          </div>
        </ActionCard>
      </div>
    </Container>
  );
}
