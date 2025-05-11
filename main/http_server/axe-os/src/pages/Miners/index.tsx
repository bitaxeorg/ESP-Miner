import { useEffect, useState } from "preact/hooks";
import { fetchMiners, SystemInfo } from "../../utils/api";
import {
  Table,
  TableHeader,
  TableBody,
  TableRow,
  TableHead,
  TableCell,
} from "../../components/Table";

export function Miners() {
  const [miners, setMiners] = useState<SystemInfo[]>([]);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const loadMiners = async () => {
      try {
        setIsLoading(true);
        const data = await fetchMiners();
        setMiners(data);
        setError(null);
      } catch (err) {
        setError("Failed to load miner data. Please check your connection.");
        console.error(err);
      } finally {
        setIsLoading(false);
      }
    };

    loadMiners();

    // Set up polling for real-time updates
    const intervalId = setInterval(loadMiners, 10000); // Refresh every 10 seconds

    // Clean up the interval on component unmount
    return () => clearInterval(intervalId);
  }, []);

  const formatHashRate = (hashRate: number): string => {
    return `${hashRate.toFixed(2)} MH/s`;
  };

  const formatFanSpeed = (speed: number): string => {
    return `${speed}%`;
  };

  const formatUptime = (seconds: number): string => {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);

    if (days > 0) {
      return `${days}d ${hours}h`;
    }
    if (hours > 0) {
      return `${hours}h ${minutes}m`;
    }
    return `${minutes}m`;
  };

  if (isLoading && miners.length === 0) {
    return (
      <div className='flex justify-center items-center min-h-[60vh]'>Loading miner data...</div>
    );
  }

  return (
    <div className='container mx-auto py-6'>
      <h1 className='text-2xl font-bold mb-6'>Miners</h1>

      {error && (
        <div className='mb-4 p-3 bg-red-500/10 border border-red-500 rounded-md text-red-500'>
          {error}
        </div>
      )}

      <div className='rounded-md border border-border'>
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>Hostname</TableHead>
              <TableHead>IP Address</TableHead>
              <TableHead>MAC Address</TableHead>
              <TableHead>Hash Rate</TableHead>
              <TableHead>Fan Speed</TableHead>
              <TableHead>Temperature</TableHead>
              <TableHead>Uptime</TableHead>
              <TableHead>Status</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {miners.length === 0 ? (
              <TableRow>
                <TableCell colSpan={8} className='text-center py-6'>
                  No miners found. Check your connection to the miner.
                </TableCell>
              </TableRow>
            ) : (
              miners.map((miner, index) => (
                <TableRow key={index}>
                  <TableCell>{miner.hostname || "Unknown"}</TableCell>
                  <TableCell>{miner.ipAddress || "Unknown"}</TableCell>
                  <TableCell>{miner.macAddr}</TableCell>
                  <TableCell>{formatHashRate(miner.hashRate)}</TableCell>
                  <TableCell>
                    {formatFanSpeed(miner.fanspeed)}
                    {miner.fanrpm ? ` (${miner.fanrpm} RPM)` : ""}
                  </TableCell>
                  <TableCell>{miner.temp ? `${miner.temp.toFixed(1)}°C` : "N/A"}</TableCell>
                  <TableCell>
                    {miner.uptimeSeconds ? formatUptime(miner.uptimeSeconds) : "N/A"}
                  </TableCell>
                  <TableCell>
                    <span
                      className={`inline-block w-3 h-3 rounded-full mr-2 ${
                        miner.status === "online"
                          ? "bg-green-500"
                          : miner.status === "warning"
                          ? "bg-yellow-500"
                          : "bg-red-500"
                      }`}
                    />
                    {miner.status || "Unknown"}
                  </TableCell>
                </TableRow>
              ))
            )}
          </TableBody>
        </Table>
      </div>

      {!isLoading && miners.length > 0 && (
        <div className='mt-2 text-right text-sm text-muted-foreground'>
          Last updated: {new Date().toLocaleTimeString()}
        </div>
      )}
    </div>
  );
}
