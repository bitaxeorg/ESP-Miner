import { useState, useEffect } from "preact/hooks";
import { Button } from "../../components/Button";
import { Terminal } from "../../components/Terminal";
import { createWebSocketConnection } from "../../utils/websocket";

export function LogsPage() {
  const [logs, setLogs] = useState<string[]>([]);
  const [isConnected, setIsConnected] = useState(false);
  const [isConnecting, setIsConnecting] = useState(false);
  const [connection, setConnection] = useState<ReturnType<typeof createWebSocketConnection> | null>(
    null
  );
  const [deviceIp, setDeviceIp] = useState("");
  const [useDirectConnect, setUseDirectConnect] = useState(false);
  const [connectionError, setConnectionError] = useState<string | null>(null);

  // Check if we're in development mode
  const isDevelopment =
    window.location.hostname === "localhost" || window.location.hostname === "127.0.0.1";

  // Clean up WebSocket connection when component unmounts
  useEffect(() => {
    return () => {
      if (connection) {
        connection.close();
      }
    };
  }, [connection]);

  function handleConnect() {
    if (isConnected && connection) {
      connection.close();
      setConnection(null);
      setIsConnected(false);
      setConnectionError(null);
      return;
    }

    // Validate IP if using direct connect
    if (useDirectConnect && (!deviceIp || !isValidIp(deviceIp))) {
      setConnectionError("Please enter a valid IP address");
      return;
    }

    setConnectionError(null);
    setIsConnecting(true);

    // Add connection start message
    setLogs((prev) => [...prev, "Connecting to log server..."]);

    try {
      const wsConnection = createWebSocketConnection(
        "/api/ws",
        (message) => {
          setLogs((prev) => [...prev, message]);
        },
        () => {
          setIsConnected(true);
          setIsConnecting(false);
          setLogs((prev) => [...prev, "Connected to log server"]);
        },
        () => {
          setIsConnected(false);
          setIsConnecting(false);
          setLogs((prev) => [...prev, "Disconnected from log server"]);
        },
        (error) => {
          setIsConnected(false);
          setIsConnecting(false);
          const errorMessage = `WebSocket error: Connection failed`;
          setLogs((prev) => [...prev, errorMessage]);
          setConnectionError(errorMessage);
        },
        {
          directConnect: useDirectConnect && isDevelopment,
          deviceIp: deviceIp || undefined,
        }
      );

      setConnection(wsConnection);
    } catch (error) {
      setIsConnecting(false);
      const errorMessage = `Failed to create WebSocket connection: ${
        error instanceof Error ? error.message : String(error)
      }`;
      setLogs((prev) => [...prev, errorMessage]);
      setConnectionError(errorMessage);
    }
  }

  function clearLogs() {
    setLogs([]);
  }

  function handleDeviceIpChange(e: Event) {
    const target = e.target as HTMLInputElement;
    setDeviceIp(target.value);
    // Clear error when user starts typing
    if (connectionError) {
      setConnectionError(null);
    }
  }

  function toggleDirectConnect() {
    setUseDirectConnect((prev) => !prev);
    // Clear error when changing connection mode
    if (connectionError) {
      setConnectionError(null);
    }
  }

  // Simple IP validation
  function isValidIp(ip: string): boolean {
    // Clean up the IP (remove any protocol prefixes if user accidentally included them)
    const cleanIp = ip.replace(/^https?:\/\/|^ws:\/\//, "");
    // Basic IP validation - accepts IPv4, hostname, or domain
    return /^[\w.-]+$/.test(cleanIp);
  }

  return (
    <div className='flex flex-col gap-4 p-4 h-full'>
      <div className='flex justify-between items-center'>
        <h1 className='text-2xl font-semibold'>System Logs</h1>
        <div className='flex gap-2'>
          <Button
            variant={isConnected ? "outline" : "default"}
            onClick={handleConnect}
            disabled={isConnecting}
          >
            {isConnecting ? "Connecting..." : isConnected ? "Disconnect" : "Fetch Logs"}
          </Button>
          <Button variant='ghost' onClick={clearLogs} disabled={logs.length === 0}>
            Clear
          </Button>
        </div>
      </div>

      {isDevelopment && (
        <div className='flex flex-col gap-2 p-4 bg-gray-100 dark:bg-gray-800 rounded-md'>
          <div className='flex items-center gap-3'>
            <label className='flex items-center gap-2'>
              <input
                type='checkbox'
                checked={useDirectConnect}
                onChange={toggleDirectConnect}
                className='h-4 w-4'
                disabled={isConnected}
              />
              <span>Direct Connect</span>
            </label>

            {useDirectConnect && (
              <div className='flex-1 flex items-center gap-2'>
                <label htmlFor='deviceIp'>Device IP:</label>
                <input
                  id='deviceIp'
                  type='text'
                  value={deviceIp}
                  onChange={handleDeviceIpChange}
                  placeholder='e.g. 10.10.0.141'
                  className='flex-1 p-1 rounded border border-gray-300 dark:border-gray-600'
                  disabled={isConnected}
                />
              </div>
            )}
          </div>

          {connectionError && <div className='text-red-500 text-sm mt-1'>{connectionError}</div>}

          <div className='text-xs text-gray-500'>
            Note: For direct connections, use the IP address of your device without any protocol
            prefix (e.g., "10.10.0.141" not "http://10.10.0.141")
          </div>
        </div>
      )}

      <Terminal logs={logs} className='flex-grow h-[calc(100vh-200px)]' />

      <div className='text-sm text-gray-500'>
        {isConnected
          ? "Connected to log server - Logs will appear in real-time"
          : isConnecting
          ? "Connecting to log server..."
          : "Disconnected - Click 'Fetch Logs' to connect"}

        {isDevelopment && useDirectConnect && deviceIp && isConnected && (
          <span> (Direct connection to {deviceIp})</span>
        )}
      </div>
    </div>
  );
}
