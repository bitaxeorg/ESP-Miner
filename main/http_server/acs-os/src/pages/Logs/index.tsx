import { useState, useEffect } from "preact/hooks";
import { Button } from "../../components/Button";
import { Switch } from "../../components/Switch";
import { Terminal } from "../../components/Terminal";
import { PageHeading } from "../../components/PageHeading";
import { Tabs } from "../../components/Tabs";
import { fetchRecentLogs, fetchErrorLogs, fetchCriticalLogs, LogEvent } from "../../utils/api";
import { Container } from "../../components/Container";

type LogLevel = 'all' | 'info' | 'warn' | 'error' | 'critical';

export function LogsPage() {
  const [logs, setLogs] = useState<string[]>([]);
  const [autoRefresh, setAutoRefresh] = useState(true);
  const [logLevel, setLogLevel] = useState<LogLevel>('all');
  const [lastFetchTime, setLastFetchTime] = useState<Date | null>(null);
  const [errorInfo, setErrorInfo] = useState<{
    totalCount: number;
    lastEvent?: number;
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
  }, [autoRefresh, logLevel]);

  // Initial fetch on mount and when log level changes
  useEffect(() => {
    fetchLogs();
  }, [logLevel]);

  // Format log event to display string
  function formatLogEvent(event: LogEvent): string {
    const timestamp = new Date(event.timestamp * 1000).toLocaleString();
    const levelBadge = event.level.toUpperCase().padEnd(8);
    const typeBadge = event.type.toUpperCase().padEnd(10);

    let logLine = `[${timestamp}] [${levelBadge}] [${typeBadge}] ${event.message}`;

    // Add additional data if present
    if (event.data) {
      let dataStr = "";

      if (typeof event.data === 'string') {
        // Data is already a string, use it directly
        dataStr = event.data;
      } else if (typeof event.data === 'object' && Object.keys(event.data).length > 0) {
        // Data is an object, stringify it
        dataStr = JSON.stringify(event.data, null, 0);
      }

      if (dataStr) {
        logLine += ` | Data: ${dataStr}`;
      }
    }

    return logLine;
  }

  // Fetch logs from appropriate API based on log level
  async function fetchLogs() {
    try {
      let response;
      let events: LogEvent[] = [];
      let totalCount = 0;
      let lastEvent: number | undefined;

      switch (logLevel) {
        case 'error':
          response = await fetchErrorLogs(100);
          events = response.errors;
          totalCount = response.totalErrors;
          lastEvent = response.lastError;
          break;

        case 'critical':
          response = await fetchCriticalLogs(100);
          events = response.critical;
          totalCount = response.totalCritical;
          lastEvent = response.lastCritical;
          break;

        case 'all':
        case 'info':
        case 'warn':
        default:
          response = await fetchRecentLogs(100);
          events = response.events;

          // Filter for specific levels if not 'all'
          if (logLevel === 'info') {
            events = events.filter(log => log.level.toLowerCase() === 'info');
          } else if (logLevel === 'warn') {
            events = events.filter(log => log.level.toLowerCase() === 'warning');
          }

          totalCount = events.length;
          lastEvent = events.length > 0 ? events[0].timestamp : undefined;
          break;
      }

      const formattedLogs = events.map(formatLogEvent);
      setLogs(formattedLogs);
      setLastFetchTime(new Date());

      // Update info for error and critical levels
      if (logLevel === 'error' || logLevel === 'critical') {
        setErrorInfo({
          totalCount,
          lastEvent,
        });
      } else {
        setErrorInfo(null);
      }

    } catch (error) {
      console.error('Failed to fetch logs:', error);
      setLogs(prev => [...prev, `Error fetching logs: ${error instanceof Error ? error.message : 'Unknown error'}`]);
    }
  }

  // Handle log level change
  function handleLogLevelChange(newLogLevel: string) {
    setLogLevel(newLogLevel as LogLevel);
  }

  // Clear logs
  function clearLogs() {
    setLogs([]);
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
          Total {logLevel === 'error' ? 'Errors' : 'Critical Events'}: {errorInfo.totalCount}
          {errorInfo.lastEvent && (
            <span className='ml-2'>
              | Last {logLevel === 'error' ? 'Error' : 'Critical Event'}: {new Date(errorInfo.lastEvent * 1000).toLocaleString()}
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
