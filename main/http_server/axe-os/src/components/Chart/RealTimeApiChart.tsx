import { useEffect, useState, useRef } from "preact/hooks";
import Chart from "./Chart";
import { ChartDataPoint } from "./config";
import { RealTimeDataFetcher, DataField } from "../../utils/realTimeDataFetcher";

interface RealTimeApiChartProps {
  title: string;
  dataFetcher: RealTimeDataFetcher;
  updateInterval?: number;
  maxDataPoints?: number;
  color?: string;
  unit?: string;
  // Chart configuration props
  defaultUseAreaChart?: boolean;
  defaultDataAggregation?: number;
  chartConfigs?: Record<string, any>;
  selectedConfigKey?: string;
  onConfigChange?: (configKey: string) => void;
}

const RealTimeApiChart = ({
  title,
  dataFetcher,
  updateInterval = 2000, // Default to 2 seconds for API calls
  maxDataPoints = 50,
  color = "#10b981", // Updated to match the new chart color
  unit = "",
  defaultUseAreaChart = true, // Changed to true for better visual appeal
  defaultDataAggregation = 5, // Increased from 2 to 5 for better aggregation
  chartConfigs,
  selectedConfigKey,
  onConfigChange,
}: RealTimeApiChartProps) => {
  const [data, setData] = useState<ChartDataPoint[]>([]);
  const [isRunning, setIsRunning] = useState(true);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  // New state for chart configuration
  const [useAreaChart, setUseAreaChart] = useState(defaultUseAreaChart);
  const [dataAggregationSeconds, setDataAggregationSeconds] = useState(defaultDataAggregation);
  const [isConfigChanging, setIsConfigChanging] = useState(false);

  const intervalRef = useRef<NodeJS.Timeout | null>(null);

  // Auto-configure plot intervals based on duration
  const getOptimalPlotInterval = (configKey: string): number => {
    if (chartConfigs && chartConfigs[configKey]) {
      return chartConfigs[configKey].intervalSeconds;
    }

    // Fallback to hardcoded values if chartConfigs not available
    switch (configKey) {
      case 'SHORT':    // 30 minutes
        return 5;      // 5s intervals
      case 'MEDIUM':   // 1 hour
        return 5;      // 5s intervals
      case 'LONG':     // 2 hours
        return 5;      // 5s intervals
      case 'EXTENDED': // 4 hours
        return 900;    // 15m intervals
      case 'FULL_DAY': // 6 hours
        return 900;    // 15m intervals
      default:
        return 5;      // Default to 5s
    }
  };

  // Get current optimal interval
  const optimalInterval = selectedConfigKey ? getOptimalPlotInterval(selectedConfigKey) : dataAggregationSeconds;

  // Auto-update aggregation when config changes
  useEffect(() => {
    if (selectedConfigKey) {
      const newInterval = getOptimalPlotInterval(selectedConfigKey);
      setDataAggregationSeconds(newInterval);
    }
  }, [selectedConfigKey]);

  // Format interval for display
  const formatInterval = (seconds: number): string => {
    if (seconds < 60) return `${seconds}s`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)}m`;
    return `${Math.floor(seconds / 3600)}h`;
  };

  // Initialize with historical data from API
  useEffect(() => {
    const initializeData = async () => {
      setIsLoading(true);
      setError(null);

      try {
        const initialData = await dataFetcher.generateInitialData(maxDataPoints);
        setData(initialData);
      } catch (err) {
        setError("Failed to load initial data");
        console.error("Failed to initialize chart data:", err);
      } finally {
        setIsLoading(false);
      }
    };

    initializeData();
  }, [dataFetcher, maxDataPoints]);

  // Start real-time updates automatically after initial data is loaded
  useEffect(() => {
    if (!isLoading && isRunning && !intervalRef.current) {
      intervalRef.current = setInterval(async () => {
        try {
          const newPoint = await dataFetcher.fetchNextPoint();

          if (newPoint) {
            setData((prevData) => {
              const newData = [...prevData, newPoint];
              // Keep only the last maxDataPoints
              return newData.slice(-maxDataPoints);
            });
            setError(null);
          }
        } catch (err) {
          console.error("Failed to fetch new data point:", err);
          setError("Failed to fetch data");
        }
      }, updateInterval);
    }

    // Cleanup interval if component unmounts or running state changes
    return () => {
      if (intervalRef.current) {
        clearInterval(intervalRef.current);
        intervalRef.current = null;
      }
    };
  }, [isLoading, isRunning, dataFetcher, maxDataPoints, updateInterval]);

  // Start/stop real-time updates
  const toggleRealTime = () => {
    if (isRunning) {
      if (intervalRef.current) {
        clearInterval(intervalRef.current);
        intervalRef.current = null;
      }
      setIsRunning(false);
    } else {
      intervalRef.current = setInterval(async () => {
        try {
          const newPoint = await dataFetcher.fetchNextPoint();

          if (newPoint) {
            setData((prevData) => {
              const newData = [...prevData, newPoint];
              // Keep only the last maxDataPoints
              return newData.slice(-maxDataPoints);
            });
            setError(null);
          }
        } catch (err) {
          console.error("Failed to fetch new data point:", err);
          setError("Failed to fetch data");
        }
      }, updateInterval);
      setIsRunning(true);
    }
  };

  const resetData = async () => {
    setIsLoading(true);
    setError(null);

    try {
      const newData = await dataFetcher.generateInitialData(maxDataPoints);
      setData(newData);
    } catch (err) {
      setError("Failed to reset data");
      console.error("Failed to reset chart data:", err);
    } finally {
      setIsLoading(false);
    }
  };

  const currentValue = data.length > 0 ? data[data.length - 1].value : 0;

  // Handle configuration changes with loading state
  const handleConfigChange = async (configKey: string) => {
    if (!onConfigChange) return;

    setIsConfigChanging(true);

    // Stop real-time updates during config change
    const wasRunning = isRunning;
    if (isRunning) {
      if (intervalRef.current) {
        clearInterval(intervalRef.current);
        intervalRef.current = null;
      }
      setIsRunning(false);
    }

    try {
      // Trigger the config change
      onConfigChange(configKey);

      // Wait a bit for the parent component to update
      await new Promise(resolve => setTimeout(resolve, 100));

      // Reload data with new configuration
      const newData = await dataFetcher.generateInitialData(maxDataPoints);
      setData(newData);

      // Restart real-time updates if they were running
      if (wasRunning) {
        setTimeout(() => {
          setIsRunning(true);
          intervalRef.current = setInterval(async () => {
            try {
              const newPoint = await dataFetcher.fetchNextPoint();
              if (newPoint) {
                setData((prevData) => {
                  const newData = [...prevData, newPoint];
                  return newData.slice(-maxDataPoints);
                });
                setError(null);
              }
            } catch (err) {
              console.error("Failed to fetch new data point:", err);
              setError("Failed to fetch data");
            }
          }, updateInterval);
        }, 200);
      }
    } catch (err) {
      console.error("Failed to change configuration:", err);
      setError("Failed to update configuration");
    } finally {
      setIsConfigChanging(false);
    }
  };

  if (isLoading && data.length === 0) {
    return (
      <div className='bg-white rounded-lg shadow-lg p-6'>
        <div className='flex justify-center items-center h-64'>
          <div className='text-gray-500'>Loading chart data...</div>
        </div>
      </div>
    );
  }

  return (
    <div className='bg-white rounded-lg shadow-lg p-6'>
      <div className='flex justify-between items-start mb-4'>
        <div>
          <h2 className='text-xl font-bold text-gray-800'>{title}</h2>
          <p className='text-2xl font-semibold' style={{ color }}>
            {currentValue.toFixed(2)} {unit}
          </p>
        </div>
        <div className='flex flex-col gap-2'>

          {/* Chart Duration Selector */}
          {chartConfigs && selectedConfigKey && onConfigChange && (
            <div className='flex justify-end'>
              <div className='flex items-center gap-2 text-sm'>
                <label className='text-gray-600 whitespace-nowrap'>Duration:</label>
                <div className='relative'>
                  <select
                    value={selectedConfigKey}
                    onChange={(e) => handleConfigChange((e.target as HTMLSelectElement).value)}
                    disabled={isConfigChanging}
                    className={`px-2 py-1 border border-gray-300 rounded text-gray-700 text-sm focus:outline-none focus:ring-2 focus:ring-blue-300 focus:border-transparent ${
                      isConfigChanging ? 'opacity-50 cursor-not-allowed' : ''
                    }`}
                  >
                    {Object.entries(chartConfigs).map(([key, config]) => (
                      <option key={key} value={key}>
                        {config.description} ({formatInterval(config.intervalSeconds)} intervals)
                      </option>
                    ))}
                  </select>
                  {isConfigChanging && (
                    <div className='absolute right-2 top-1/2 transform -translate-y-1/2'>
                      <div className='w-3 h-3 border border-blue-500 border-t-transparent rounded-full animate-spin'></div>
                    </div>
                  )}
                </div>
              </div>
            </div>
          )}
        </div>
      </div>

      <div className='mb-4'>
        <span
          className={`inline-block w-3 h-3 rounded-full mr-2 ${
            isConfigChanging
              ? "bg-orange-500 animate-pulse"
              : isRunning
              ? "bg-green-500 animate-pulse"
              : "bg-gray-400"
          }`}
        ></span>
        <span className='text-sm text-gray-600'>
          {isConfigChanging
            ? "Updating configuration..."
            : isRunning
            ? "Live"
            : "Paused"} • {data.length} data points
          {useAreaChart && <span className='text-green-500 ml-2'>• Area Chart</span>}
          {dataAggregationSeconds > 1 && <span className='text-purple-500 ml-2'>• Aggregated({formatInterval(dataAggregationSeconds)})</span>}
          {error && <span className='text-red-500 ml-2'>• {error}</span>}
        </span>
      </div>

      <div className='relative'>
        {isConfigChanging && (
          <div className='absolute inset-0 bg-white/80 backdrop-blur-sm z-10 flex items-center justify-center rounded-lg'>
            <div className='flex items-center gap-3 text-gray-600'>
              <div className='w-5 h-5 border-2 border-blue-500 border-t-transparent rounded-full animate-spin'></div>
              <span className='text-sm font-medium'>Updating chart configuration...</span>
            </div>
          </div>
        )}
        <Chart
          data={data}
          seriesOptions={{ color }}
          useAreaChart={useAreaChart}
          dataAggregationSeconds={dataAggregationSeconds}
        />
      </div>
    </div>
  );
};

export default RealTimeApiChart;
