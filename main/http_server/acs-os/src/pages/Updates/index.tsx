import { useState, useEffect, useRef } from "preact/hooks";
import { Button } from "../../components/Button";
import {
  fetchMiners,
  uploadFirmware,
  uploadWebApp,
  FIRMWARE_LATEST_URL,
  WEBAPP_LATEST_URL,
  getVersionInfo,
  VersionInfo,
} from "../../utils/api";
import { useToast } from "../../context/ToastContext";
import { Container } from "../../components/Container";
import { PageHeading } from "../../components/PageHeading";
import { VersionDisplay, UpdateBadge } from "../../components/VersionDisplay";

interface ActionCardProps {
  title: string | preact.ComponentChildren;
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

export function UpdatesPage() {
  const { showToast } = useToast();
  const [isUpdating, setIsUpdating] = useState(false);
  const [isWebAppUpdating, setIsWebAppUpdating] = useState(false);

  const [firmwareFile, setFirmwareFile] = useState<File | null>(null);
  const [webAppFile, setWebAppFile] = useState<File | null>(null);
  const [versionInfo, setVersionInfo] = useState<VersionInfo>({
    current: null,
    latest: null,
    hasUpdate: false,
    isLoading: true,
    error: null,
    lastChecked: null,
  });
  const fileInputRef = useRef<HTMLInputElement>(null);
  const webAppFileInputRef = useRef<HTMLInputElement>(null);

  const fetchSystemInfo = async () => {
    try {
      setVersionInfo((prev) => ({ ...prev, isLoading: true }));
      const miners = await fetchMiners();
      // Use the first miner (current implementation only returns one)
      const currentMiner = miners.length > 0 ? miners[0] : null;

      // Fetch version information
      const versionData = await getVersionInfo(currentMiner?.version || null);
      setVersionInfo(versionData);
    } catch (error) {
      showToast(
        `Error fetching system info: ${error instanceof Error ? error.message : "Unknown error"}`,
        "error"
      );
      // Set error state for version info
      setVersionInfo((prev) => ({
        ...prev,
        isLoading: false,
        error: error instanceof Error ? error.message : "Unknown error",
        lastChecked: new Date(),
      }));
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
        // Refresh version info to get the updated firmware version
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
        // Note: Web app updates don't change firmware version
        // await fetchSystemInfo();
      } else {
        showToast(`Failed to update web app: ${result.message}`, "error");
      }
    } catch (error) {
      showToast(`Error: ${error instanceof Error ? error.message : "Unknown error"}`, "error");
    } finally {
      setIsWebAppUpdating(false);
    }
  };

  return (
    <Container>
      <PageHeading
        title='Firmware & Web App Updates'
        subtitle='Keep your device up-to-date with the latest firmware and web interface.'
        link='https://help.advancedcryptoservices.com/en/articles/11519242-firmware-updates'
      />

      <ActionCard
        title={
          <div className='flex items-center gap-3'>
            <span>Update Device Firmware</span>
            <UpdateBadge hasUpdate={versionInfo.hasUpdate} />
          </div>
        }
        description={
          versionInfo.hasUpdate
            ? "Download the latest firmware and then upload it to update your device."
            : "Your device is running the latest firmware version."
        }
        link={FIRMWARE_LATEST_URL}
      >
        <div className='space-y-4'>
          <VersionDisplay versionInfo={versionInfo} className='mb-4' />

          {versionInfo.hasUpdate ? (
            // Show download/install UI when update is available
            <>
              <div className='mb-3'>
                <p className='text-md text-white mb-2'>1. Download the latest firmware version</p>
                <div className='flex flex-wrap gap-3'>
                  <Button
                    as='a'
                    href={FIRMWARE_LATEST_URL}
                    download='esp-miner.bin'
                    className='bg-blue-600 hover:bg-blue-700'
                    size='sm'
                  >
                    <span className='sm:hidden'>Download Firmware</span>
                    <span className='hidden sm:inline'>Download Latest Firmware</span>
                  </Button>
                </div>
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

              <div className='mt-8'>
                <label className='block text-md text-white mb-2'>
                  2. Upload firmware file you just downloaded
                </label>
                <div className='flex items-center gap-2'>
                  <Button
                    variant='default'
                    type='button'
                    size='sm'
                    onClick={handleFirmwareFileClick}
                    className='bg-blue-600 hover:bg-blue-700'
                  >
                    <span className='sm:hidden'>Choose File</span>
                    <span className='hidden sm:inline'>Choose File to Upload</span>
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
                {firmwareFile && (
                  <p className='text-sm text-green-500 mt-2'>
                    ✓ File selected: {firmwareFile.name}
                  </p>
                )}
              </div>

              <div className='mt-8'>
                <p className='text-md text-white mb-2'>3. Click install to flash your device</p>
                <Button
                  onClick={handleFirmwareUpload}
                  disabled={isUpdating || !firmwareFile}
                  className='bg-blue-600 hover:bg-blue-700'
                  size='sm'
                >
                  {isUpdating ? "Installing..." : "Install Firmware"}
                </Button>
              </div>
            </>
          ) : (
            // Show "Check for Updates" button when up-to-date
            <div className='py-4'>
              <Button
                onClick={fetchSystemInfo}
                disabled={versionInfo.isLoading}
                className='bg-blue-600 hover:bg-blue-700'
              >
                {versionInfo.isLoading ? "Checking..." : "Check for Updates"}
              </Button>
            </div>
          )}
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
            <div className='flex flex-col sm:flex-row sm:items-center gap-3'>
              <div className='flex-1'>
                <div className='flex items-center gap-2'>
                  <Button
                    variant='outline'
                    type='button'
                    size='sm'
                    onClick={handleWebAppFileClick}
                    className='border-blue-600 text-white hover:bg-blue-600'
                  >
                    <span className='sm:hidden'>Choose</span>
                    <span className='hidden sm:inline'>2. Choose File</span>
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
                disabled={isWebAppUpdating || !webAppFile}
                className='bg-blue-600 hover:bg-blue-700 w-full sm:w-auto'
              >
                {isWebAppUpdating ? (
                  "Updating..."
                ) : (
                  <>
                    <span className='sm:hidden'>Install</span>
                    <span className='hidden sm:inline'>3. Install</span>
                  </>
                )}
              </Button>
            </div>
            {webAppFile && (
              <p className='text-sm text-green-500 mt-2'>✓ File selected: {webAppFile.name}</p>
            )}
          </div>
        </div>
      </ActionCard>
    </Container>
  );
}
