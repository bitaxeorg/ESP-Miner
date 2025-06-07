import { VersionInfo } from "../utils/api";

interface VersionDisplayProps {
  versionInfo: VersionInfo;
  className?: string;
}

export function VersionDisplay({ versionInfo, className = "" }: VersionDisplayProps) {
  const { current, latest, hasUpdate, isLoading, error, lastChecked } = versionInfo;

  const formatLastChecked = (date: Date | null) => {
    if (!date) return "";
    const now = new Date();
    const diffMs = now.getTime() - date.getTime();
    const diffMins = Math.floor(diffMs / (1000 * 60));

    if (diffMins < 1) return "just now";
    if (diffMins < 60) return `${diffMins}m ago`;

    const diffHours = Math.floor(diffMins / 60);
    if (diffHours < 24) return `${diffHours}h ago`;

    return date.toLocaleDateString();
  };

  if (isLoading) {
    return (
      <div className={`text-sm text-[#8B96A5] ${className}`}>
        <div className='flex items-center gap-2'>
          <div className='w-2 h-2 bg-blue-500 rounded-full animate-pulse' />
          Checking for updates...
        </div>
      </div>
    );
  }

  if (error) {
    return (
      <div className={`text-sm ${className}`}>
        <div className='flex items-center gap-2 text-yellow-500'>
          <div className='w-2 h-2 bg-yellow-500 rounded-full' />
          <span>Current: {current || "Unknown"}</span>
        </div>
        <div className='text-xs text-[#8B96A5] mt-1'>Unable to check for updates: {error}</div>
      </div>
    );
  }

  return (
    <div className={`text-sm ${className}`}>
      {/* Status and last checked on top line */}
      <div className='flex items-center gap-4 mb-2'>
        <div className='flex items-center gap-2'>
          {hasUpdate ? (
            <>
              <div className='w-2 h-2 bg-orange-500 rounded-full animate-pulse' />
              <span className='text-orange-500 font-medium'>Update Available</span>
            </>
          ) : current && latest ? (
            <>
              <div className='w-2 h-2 bg-green-500 rounded-full' />
              <span className='text-green-500'>Up to Date</span>
            </>
          ) : (
            <>
              <div className='w-2 h-2 bg-blue-500 rounded-full' />
              <span className='text-blue-500'>Version Check Complete</span>
            </>
          )}
        </div>

        {lastChecked && (
          <div className='text-xs text-[#8B96A5]'>
            Last checked: {formatLastChecked(lastChecked)}
          </div>
        )}
      </div>

      {/* Current version */}
      <div className='flex items-center gap-2'>
        <span className='text-[#8B96A5]'>Current:</span>
        <span className='text-white font-medium'>{current || "Unknown"}</span>
      </div>

      {/* Latest version */}
      {latest && (
        <div className='flex items-center gap-2 mt-1'>
          <span className='text-[#8B96A5]'>Latest:</span>
          <span className='text-white font-medium'>{latest}</span>
        </div>
      )}
    </div>
  );
}

interface UpdateBadgeProps {
  hasUpdate: boolean;
  className?: string;
}

export function UpdateBadge({ hasUpdate, className = "" }: UpdateBadgeProps) {
  if (!hasUpdate) return null;

  return (
    <div
      className={`inline-flex items-center gap-1 px-2 py-1 rounded-full bg-orange-500/20 border border-orange-500/30 ${className}`}
    >
      <div className='w-1.5 h-1.5 bg-orange-500 rounded-full animate-pulse' />
      <span className='text-xs font-medium text-orange-500'>UPDATE</span>
    </div>
  );
}
