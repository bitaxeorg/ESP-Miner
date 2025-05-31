import { useState, useEffect, useRef } from "preact/hooks";
import { Button } from "../../components/Button";
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

export default function UpdatesPage() {
  const { showToast } = useToast();
  const [isUpdating, setIsUpdating] = useState(false);
  const [isWebAppUpdating, setIsWebAppUpdating] = useState(false);
  const [minerInfo, setMinerInfo] = useState<SystemInfo | null>(null);
  const [isLoading, setIsLoading] = useState(true);
  const [firmwareFile, setFirmwareFile] = useState<File | null>(null);
  const [webAppFile, setWebAppFile] = useState<File | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const webAppFileInputRef = useRef<HTMLInputElement>(null);

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

  const handleFirmwareUpload = async () => {
    if (!firmwareFile) {
      showToast("Please select a firmware file first", "error");
      return;
    }

    setIsUpdating(true);
    showToast("Uploading and installing firmware...", "info");

    try {
      const result = await uploadFirmware(firmwareFile);
      if (result.success) {
        showToast(result.message, "success");
        setFirmwareFile(null);
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

  const handleFirmwareFileChange = (e: Event) => {
    const file = (e.target as HTMLInputElement).files?.[0];
    if (file) {
      setFirmwareFile(file);
      showToast(`Selected firmware file: ${file.name}`, "info");
    }
  };

  const handleFirmwareFileClick = () => {
    fileInputRef.current?.click();
  };

  const handleWebAppFileClick = () => {
    webAppFileInputRef.current?.click();
  };

  const handleWebAppFileChange = (e: Event) => {
    const file = (e.target as HTMLInputElement).files?.[0];
    if (file) {
      setWebAppFile(file);
      showToast(`Selected web app file: ${file.name}`, "info");
    }
  };

  const handleWebAppUpload = async () => {
    if (!webAppFile) {
      showToast("Please select a web app file first", "error");
      return;
    }

    setIsWebAppUpdating(true);
    showToast("Uploading and installing web app...", "info");

    try {
      const result = await uploadWebApp(webAppFile);
      if (result.success) {
        showToast(result.message, "success");
        setWebAppFile(null);
        // Refresh system info if needed
        await fetchSystemInfo();
      } else {
        showToast(`Failed to update web app: ${result.message}`, "error");
      }
    } catch (error) {
      showToast(`Error: ${error instanceof Error ? error.message : "Unknown error"}`, "error");
    } finally {
      setIsWebAppUpdating(false);
    }
  };

  const getFirmwareDescription = () => {
    if (isLoading) return "Loading current firmware information...";
    let baseText = minerInfo?.version ? `Current version: ${minerInfo.version}. ` : "";
    return `${baseText}Download the latest firmware and then upload it to update your device.`;
  };

  return (
    <Container>
      <h2 className='text-xl font-bold text-white mb-6'>Firmware & Web App Updates</h2>
      <div className='grid grid-cols-1 gap-6 max-w-4xl mx-auto'>
        <ActionCard
          title='Update Device Firmware'
          description={getFirmwareDescription()}
          link={FIRMWARE_LATEST_URL}
        >
          <div className='space-y-4'>
            <div className='flex flex-wrap gap-3'>
              <Button
                as='a'
                href={FIRMWARE_LATEST_URL}
                download='esp-miner.bin'
                className='bg-blue-600 hover:bg-blue-700'
              >
                <span className='sm:hidden'>Download Firmware</span>
                <span className='hidden sm:inline'>1. Download Latest Firmware</span>
              </Button>
            </div>

            <div className='mt-2 text-sm text-[#8B96A5]'>
              <p>
                If the button doesn't work,{" "}
                <a
                  href={FIRMWARE_LATEST_URL}
                  download='esp-miner.bin'
                  className='text-blue-400 underline'
                >
                  click here
                </a>{" "}
                to download directly.
              </p>
            </div>

            <div className='mt-4'>
              <label className='block text-sm text-[#8B96A5] mb-2'>
                2. Upload downloaded firmware:
              </label>
              <div className='flex items-center gap-3'>
                <div className='flex-1'>
                  <div className='flex items-center gap-2'>
                    <Button
                      variant='outline'
                      type='button'
                      size='sm'
                      onClick={handleFirmwareFileClick}
                    >
                      Choose File
                    </Button>
                    <span className='text-sm text-[#8B96A5]'>
                      {firmwareFile ? firmwareFile.name : "No file selected"}
                    </span>
                    <input
                      ref={fileInputRef}
                      type='file'
                      accept='.bin'
                      onChange={handleFirmwareFileChange}
                      className='sr-only' // Hidden but accessible
                    />
                  </div>
                </div>
                <Button
                  onClick={handleFirmwareUpload}
                  disabled={isUpdating}
                  className='bg-blue-600 hover:bg-blue-700'
                >
                  {isUpdating ? "Updating..." : "Install"}
                </Button>
              </div>
              {firmwareFile && (
                <p className='text-sm text-green-500 mt-2'>✓ File selected: {firmwareFile.name}</p>
              )}
            </div>
          </div>
        </ActionCard>
        <ActionCard
          title='Update Web App'
          description='Download the latest web app and then upload it to update your device.'
          link={WEBAPP_LATEST_URL}
        >
          <div className='space-y-4'>
            <div className='flex flex-wrap gap-3'>
              <Button
                as='a'
                href={WEBAPP_LATEST_URL}
                download='www.bin'
                className='bg-blue-600 hover:bg-blue-700'
              >
                <span className='sm:hidden'>Download www.bin</span>
                <span className='hidden sm:inline'>1. Download Latest Web App</span>
              </Button>
            </div>

            <div className='mt-2 text-sm text-[#8B96A5]'>
              <p>
                If the button doesn't work,{" "}
                <a href={WEBAPP_LATEST_URL} download='www.bin' className='text-blue-400 underline'>
                  click here
                </a>{" "}
                to download directly.
              </p>
            </div>

            <div className='mt-4'>
              <label className='block text-sm text-[#8B96A5] mb-2'>
                2. Upload downloaded web app:
              </label>
              <div className='flex items-center gap-3'>
                <div className='flex-1'>
                  <div className='flex items-center gap-2'>
                    <Button
                      variant='outline'
                      type='button'
                      size='sm'
                      onClick={handleWebAppFileClick}
                    >
                      Choose File
                    </Button>
                    <span className='text-sm text-[#8B96A5]'>
                      {webAppFile ? webAppFile.name : "No file selected"}
                    </span>
                    <input
                      ref={webAppFileInputRef}
                      type='file'
                      accept='.bin'
                      onChange={handleWebAppFileChange}
                      className='sr-only' // Hidden but accessible
                    />
                  </div>
                </div>
                <Button
                  onClick={handleWebAppUpload}
                  disabled={isWebAppUpdating}
                  className='bg-blue-600 hover:bg-blue-700'
                >
                  {isWebAppUpdating ? "Updating..." : "Install"}
                </Button>
              </div>
              {webAppFile && (
                <p className='text-sm text-green-500 mt-2'>✓ File selected: {webAppFile.name}</p>
              )}
            </div>
          </div>
        </ActionCard>
      </div>
    </Container>
  );
}
