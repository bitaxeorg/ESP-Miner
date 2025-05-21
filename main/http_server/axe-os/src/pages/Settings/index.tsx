import { useState, useEffect } from "preact/hooks";
import { Button } from "../../components/Button";
import {
  fetchMiners,
  updateFirmware,
  uploadFirmware,
  FIRMWARE_LATEST_URL,
  SystemInfo,
} from "../../utils/api";
import { useToast } from "../../context/ToastContext";
import { Container } from "../../components/Container";

interface ActionCardProps {
  title: string;
  description: string;
  buttonText: string;
  onAction: () => void;
  secondaryButtonText?: string;
  onSecondaryAction?: () => void;
  link?: string;
}

function ActionCard({
  title,
  description,
  buttonText,
  onAction,
  secondaryButtonText,
  onSecondaryAction,
  link,
}: ActionCardProps) {
  return (
    <div className='bg-[var(--card-bg)] rounded-xl border border-[#1C2F52] p-6 shadow-md'>
      <h3 className='text-lg font-medium text-white mb-2'>{title}</h3>
      <p className='text-[#8B96A5] mb-4'>
        {description}
        {link && (
          <a
            href={link}
            target='_blank'
            rel='noopener noreferrer'
            className='text-blue-400 hover:underline ml-1'
          >
            here
          </a>
        )}
        .
      </p>
      <div className='flex flex-wrap gap-3'>
        <Button onClick={onAction}>{buttonText}</Button>
        {secondaryButtonText && onSecondaryAction && (
          <Button variant='outline' onClick={onSecondaryAction}>
            {secondaryButtonText}
          </Button>
        )}
      </div>
    </div>
  );
}

export function Settings() {
  const { showToast } = useToast();
  const [isUpdating, setIsUpdating] = useState(false);
  const [minerInfo, setMinerInfo] = useState<SystemInfo | null>(null);
  const [isLoading, setIsLoading] = useState(true);

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

  useEffect(() => {
    fetchSystemInfo();
  }, [showToast]);

  const handleFirmwareUpdate = async () => {
    setIsUpdating(true);
    showToast("Downloading and installing latest firmware...", "info");

    try {
      const result = await updateFirmware();
      if (result.success) {
        showToast(result.message, "success");
        // Refresh system info to get the updated firmware version
        await fetchSystemInfo();
      } else {
        showToast(`Failed to update firmware: ${result.message}`, "error");
      }
    } catch (error) {
      showToast(`Error: ${error instanceof Error ? error.message : "Unknown error"}`, "error");
    } finally {
      setIsUpdating(false);
    }
  };

  const handleWebAppUpdate = () => {
    // Create a hidden file input
    const fileInput = document.createElement("input");
    fileInput.type = "file";
    fileInput.accept = ".zip";

    fileInput.onchange = (e) => {
      const file = (e.target as HTMLInputElement).files?.[0];
      if (file) {
        showToast(`Selected web app file: ${file.name}`, "info");
        // TODO: Implement web app update API call
      }
    };

    fileInput.click();
  };

  const getFirmwareDescription = () => {
    if (isLoading) return "Loading current firmware information...";
    if (minerInfo?.version) {
      return `Current version: ${minerInfo.version}. Automatically download and install the latest firmware version available`;
    }
    return "Automatically download and install the latest firmware version available";
  };

  return (
    <Container>
      <h2 className='text-xl font-bold text-white mb-6'>System Settings</h2>
      <div className='grid grid-cols-1 md:grid-cols-2 gap-6'>
        <ActionCard
          title='Update Device Firmware'
          description={getFirmwareDescription()}
          buttonText={isUpdating ? "Updating..." : "Update to Latest Firmware"}
          onAction={handleFirmwareUpdate}
          // link={FIRMWARE_LATEST_URL}
        />
        <ActionCard
          title='Update Web App'
          description='Upload the latest web app version available'
          buttonText='Choose Web App File'
          onAction={handleWebAppUpdate}
        />
      </div>
    </Container>
  );
}
