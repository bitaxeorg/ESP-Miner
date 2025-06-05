import { useState, useEffect } from "preact/hooks";
import { Button } from "../../components/Button";
import { Terminal } from "../../components/Terminal";
import { fetchRecentLogs, fetchErrorLogs, LogEvent } from "../../utils/api";

type LogViewType = 'recent' | 'errors';

export function LogsPage() {
  const [logs, setLogs] = useState<string[]>([]);
  const [isLoading, setIsLoading] = useState(false);
  const [autoRefresh, setAutoRefresh] = useState(false);
  const [viewType, setViewType] = useState<LogViewType>('recent');
  const [lastFetchTime, setLastFetchTime] = useState<Date | null>(null);
  const [errorInfo, setErrorInfo] = useState<{
    totalErrors: number;
    lastError?: number;
  } | null>(null);

  // Auto-refresh interval (10 seconds)
  useEffect(() => {
    let intervalId: number | null = null;

    if (autoRefresh) {
      intervalId = window.setInterval(() => {
        fetchLogs();
      }, 10000);
    }

    return () => {
      if (intervalId) {
        clearInterval(intervalId);
      }
    };
  }, [autoRefresh, viewType]);

  // Format log event to display string
  function formatLogEvent(event: LogEvent): string {
    const timestamp = new Date(event.timestamp * 1000).toLocaleString();
    const levelBadge = event.level.toUpperCase().padEnd(8);
    const typeBadge = event.type.toUpperCase().padEnd(10);

    let logLine = `[${timestamp}] [${levelBadge}] [${typeBadge}] ${event.message}`;

    // Add additional data if present
    if (event.data && Object.keys(event.data).length > 0) {
      const dataStr = JSON.stringify(event.data, null, 0);
      logLine += ` | Data: ${dataStr}`;
    }

    return logLine;
  }

  // Fetch logs from API
  async function fetchLogs() {
    setIsLoading(true);

    try {
      if (viewType === 'recent') {
        const response = await fetchRecentLogs(100);
        const formattedLogs = response.events.map(formatLogEvent);
        setLogs(formattedLogs);
        setErrorInfo(null);
      } else {
        const response = await fetchErrorLogs();
        const formattedLogs = response.errors.map(formatLogEvent);
        setLogs(formattedLogs);
        setErrorInfo({
          totalErrors: response.totalErrors,
          lastError: response.lastError,
        });
      }

      setLastFetchTime(new Date());
    } catch (error) {
      console.error('Failed to fetch logs:', error);
      setLogs(prev => [...prev, `Error fetching logs: ${error instanceof Error ? error.message : 'Unknown error'}`]);
    } finally {
      setIsLoading(false);
    }
  }

  // Handle view type change
  function handleViewTypeChange(newViewType: LogViewType) {
    setViewType(newViewType);
    setLogs([]); // Clear current logs when switching views
    setErrorInfo(null);
  }

  // Clear logs
  function clearLogs() {
    setLogs([]);
    setErrorInfo(null);
  }

  // Toggle auto-refresh
  function toggleAutoRefresh() {
    setAutoRefresh(prev => !prev);
  }

  return (
    <div className='flex flex-col gap-4 p-4 h-full'>
      <div className='flex justify-between items-center'>
        <h1 className='text-2xl font-semibold'>System Logs</h1>
        <div className='flex gap-2'>
          <Button
            variant={autoRefresh ? "default" : "outline"}
            onClick={toggleAutoRefresh}
            className={autoRefresh ? "bg-green-600 hover:bg-green-700" : ""}
          >
            {autoRefresh ? "Auto-Refresh On" : "Auto-Refresh Off"}
          </Button>
          <Button
            variant="default"
            onClick={fetchLogs}
            disabled={isLoading}
            className="bg-blue-600 hover:bg-blue-700"
          >
            {isLoading ? "Loading..." : "Refresh"}
          </Button>
          <Button variant='ghost' onClick={clearLogs} disabled={logs.length === 0}>
            Clear
          </Button>
        </div>
      </div>

      <div className='flex gap-2 items-center'>
        <span className='text-sm font-medium'>View:</span>
        <Button
          variant={viewType === 'recent' ? "default" : "outline"}
          onClick={() => handleViewTypeChange('recent')}
          size="sm"
        >
          Recent Logs
        </Button>
        <Button
          variant={viewType === 'errors' ? "default" : "outline"}
          onClick={() => handleViewTypeChange('errors')}
          size="sm"
        >
          Errors Only
        </Button>

        {errorInfo && (
          <span className='text-sm text-red-600 ml-4'>
            Total Errors: {errorInfo.totalErrors}
            {errorInfo.lastError && (
              <span className='ml-2'>
                | Last Error: {new Date(errorInfo.lastError * 1000).toLocaleString()}
              </span>
            )}
          </span>
        )}
      </div>

      <Terminal logs={logs} className='flex-grow h-[calc(100vh-200px)]' />

      <div className='text-sm text-gray-500'>
        {lastFetchTime ? (
          <span>
            Last updated: {lastFetchTime.toLocaleString()}
            {autoRefresh && " (Auto-refresh enabled - updates every 10 seconds)"}
          </span>
        ) : (
          <span>Click 'Refresh' to load logs</span>
        )}

        {logs.length > 0 && (
          <span className='ml-4'>
            Showing {logs.length} {viewType === 'recent' ? 'recent events' : 'error events'}
          </span>
        )}
      </div>
    </div>
  );
}
