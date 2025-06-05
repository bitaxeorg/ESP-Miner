import { useState, useEffect } from "preact/hooks";
import { Button } from "../../components/Button";
import { Switch } from "../../components/Switch";
import { Terminal } from "../../components/Terminal";
import { PageHeading } from "../../components/PageHeading";
import { Tabs } from "../../components/Tabs";
import { fetchRecentLogs, fetchErrorLogs, LogEvent } from "../../utils/api";
import { Container } from "../../components/Container";

type LogLevel = 'all' | 'info' | 'warn' | 'error' | 'critical';

export function LogsPage() {
  const [logs, setLogs] = useState<string[]>([]);
  const [allLogs, setAllLogs] = useState<LogEvent[]>([]);
  const [isLoading, setIsLoading] = useState(false);
  const [autoRefresh, setAutoRefresh] = useState(true);
  const [logLevel, setLogLevel] = useState<LogLevel>('all');
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
  }, [autoRefresh]);

  // Initial fetch on mount
  useEffect(() => {
    fetchLogs();
  }, []);

  // Filter logs when log level changes
  useEffect(() => {
    filterLogs();
  }, [logLevel, allLogs]);

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

  // Filter logs based on selected level
  function filterLogs() {
    let filteredLogs = allLogs;

    if (logLevel !== 'all') {
      filteredLogs = allLogs.filter(log =>
        log.level.toLowerCase() === logLevel.toLowerCase()
      );
    }

    const formattedLogs = filteredLogs.map(formatLogEvent);
    setLogs(formattedLogs);

    // Update error info for error level
    if (logLevel === 'error') {
      const errorLogs = filteredLogs;
      setErrorInfo({
        totalErrors: errorLogs.length,
        lastError: errorLogs.length > 0 ? errorLogs[0].timestamp : undefined,
      });
    } else {
      setErrorInfo(null);
    }
  }

  // Fetch logs from API
  async function fetchLogs() {
    setIsLoading(true);

    try {
      const response = await fetchRecentLogs(500); // Fetch more logs for better filtering
      setAllLogs(response.events);
      setLastFetchTime(new Date());
    } catch (error) {
      console.error('Failed to fetch logs:', error);
      setLogs(prev => [...prev, `Error fetching logs: ${error instanceof Error ? error.message : 'Unknown error'}`]);
    } finally {
      setIsLoading(false);
    }
  }

  // Handle log level change
  function handleLogLevelChange(newLogLevel: string) {
    setLogLevel(newLogLevel as LogLevel);
  }

  // Clear logs
  function clearLogs() {
    setLogs([]);
    setAllLogs([]);
    setErrorInfo(null);
  }

  const tabs = [
    { id: 'all', label: 'All' },
    { id: 'info', label: 'Info' },
    { id: 'warn', label: 'Warn' },
    { id: 'error', label: 'Error' },
    { id: 'critical', label: 'Critical' },
  ];

  return (
    <Container>
      <PageHeading
        title='System Logs'
        subtitle='Monitor system events and debug information in real-time.'
        link='https://help.advancedcryptoservices.com/en/articles/11517662-system-logs'
      />

      <div className='flex items-center justify-between gap-4 mb-4'>
        <div className='flex-1'>
          <Tabs tabs={tabs} activeTab={logLevel} onTabChange={handleLogLevelChange} />
        </div>
        <div className='flex items-center gap-4'>
          <div className='flex items-center gap-2'>
            <Switch
              checked={autoRefresh}
              onCheckedChange={setAutoRefresh}
              aria-label='Auto-refresh logs'
              className={`focus-visible:ring-blue-500 ${autoRefresh ? '!bg-blue-600' : ''}`}
            />
            <span className='text-sm text-slate-300'>Auto-Refresh</span>
          </div>
          <Button variant='ghost' onClick={clearLogs} disabled={logs.length === 0} className="text-slate-400 hover:text-slate-300">
            Clear
          </Button>
        </div>
      </div>

      {errorInfo && (
        <div className='text-sm text-gray-400 mb-1'>
          Total Errors: {errorInfo.totalErrors}
          {errorInfo.lastError && (
            <span className='ml-2'>
              | Last Error: {new Date(errorInfo.lastError * 1000).toLocaleString()}
            </span>
          )}
        </div>
      )}

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
            Showing {logs.length} {logLevel === "all" ? "events" : `${logLevel} events`}
          </span>
        )}
      </div>
    </Container>
  );
}
