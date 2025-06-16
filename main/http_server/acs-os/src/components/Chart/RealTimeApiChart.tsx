import { useEffect, useState, useRef, useCallback } from "preact/hooks";
import Chart from "./Chart";
import { ChartDataPoint } from "./config";
import { RealTimeDataFetcher } from "../../utils/realTimeDataFetcher";

interface RealTimeApiChartProps {
  title: string;
  dataFetcher: RealTimeDataFetcher;
  updateInterval?: number;
  maxDataPoints?: number;
  color?: string;
  // Chart configuration props
  defaultUseAreaChart?: boolean;
  defaultDataAggregation?: number;
  chartConfigs?: Record<string, any>;
  selectedConfigKey?: string;
  onConfigChange?: (configKey: string) => void;
  lineStyle?: "solid" | "dotted" | "dashed";
}

const RealTimeApiChart = ({
  title,
  dataFetcher,
  updateInterval = 2000, // Default to 2 seconds for API calls
  maxDataPoints = 50,
  color = "#10b981", // Updated to match the new chart color
  defaultUseAreaChart = true, // Changed to true for better visual appeal
  defaultDataAggregation = 5, // Increased from 2 to 5 for better aggregation
  chartConfigs,
  selectedConfigKey,
  onConfigChange,
  lineStyle = "solid",
}: RealTimeApiChartProps) => {
  const [data, setData] = useState<ChartDataPoint[]>([]);
  const [isRunning, setIsRunning] = useState(true);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  // New state for chart configuration
  const [useAreaChart] = useState(defaultUseAreaChart);
  const [dataAggregationSeconds, setDataAggregationSeconds] = useState(defaultDataAggregation);
  const [isConfigChanging, setIsConfigChanging] = useState(false);

  const intervalRef = useRef<NodeJS.Timeout | null>(null);

    // localStorage persistence utilities
  const getStorageKey = useCallback((dataField: string) => `chart_data_${dataField}`, []);

  const saveDataToStorage = useCallback((chartData: ChartDataPoint[], dataField: string) => {
    try {
      if (typeof window !== 'undefined' && window.localStorage) {
        window.localStorage.setItem(getStorageKey(dataField), JSON.stringify({
          timestamp: Date.now(),
          data: chartData.slice(-100) // Only store last 100 points
        }));
      }
    } catch (err) {
      console.warn('Failed to save chart data to localStorage:', err);
    }
  }, [getStorageKey]);

  const loadDataFromStorage = useCallback((dataField: string): ChartDataPoint[] => {
    try {
      if (typeof window !== 'undefined' && window.localStorage) {
        const stored = window.localStorage.getItem(getStorageKey(dataField));
        if (!stored) return [];

        const parsed = JSON.parse(stored);
        const age = Date.now() - parsed.timestamp;

        // Only use stored data if it's less than 30 minutes old
        if (age < 30 * 60 * 1000) {
          return parsed.data || [];
        }
      }
    } catch (err) {
      console.warn('Failed to load chart data from localStorage:', err);
    }
    return [];
  }, [getStorageKey]);

  // Auto-configure plot intervals based on duration
  const getOptimalPlotInterval = useCallback((configKey: string): number => {
    if (chartConfigs && chartConfigs[configKey]) {
      return chartConfigs[configKey].intervalSeconds;
    }

    // Fallback to hardcoded values if chartConfigs not available
    switch (configKey) {
      case "SHORT": // 30 minutes
        return 5; // 5s intervals
      case "MEDIUM": // 1 hour
        return 5; // 5s intervals
      case "LONG": // 2 hours
        return 5; // 5s intervals
      case "EXTENDED": // 4 hours
        return 900; // 15m intervals
      case "FULL_DAY": // 6 hours
        return 900; // 15m intervals
      default:
        return 5; // Default to 5s
    }
  }, [chartConfigs]);

  // Auto-update aggregation when config changes
  useEffect(() => {
    if (selectedConfigKey) {
      const newInterval = getOptimalPlotInterval(selectedConfigKey);
      setDataAggregationSeconds(newInterval);
    }
  }, [selectedConfigKey, getOptimalPlotInterval]);

  // Format interval for display
  const formatInterval = (seconds: number): string => {
    if (seconds < 60) return `${seconds}s`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)}m`;
    return `${Math.floor(seconds / 3600)}h`;
  };

  // Initialize with localStorage data or single current data point
  useEffect(() => {
    const initializeData = async () => {
      setIsLoading(true);
      setError(null);

      try {
        // First, try to load from localStorage
        const storedData = loadDataFromStorage(dataFetcher.getDataField());

        if (storedData.length > 0) {
          setData(storedData);
          setIsLoading(false);
          return;
        }

        // If no stored data, start with current data point
        const initialData = await dataFetcher.generateInitialData(1);
        setData(initialData);
      } catch (err) {
        setError("Failed to load initial data");
        console.error("Failed to initialize chart data:", err);
      } finally {
        setIsLoading(false);
      }
    };

    initializeData();
  }, [dataFetcher]);

  // Save data to localStorage when it updates
  useEffect(() => {
    if (data.length > 0) {
      saveDataToStorage(data, dataFetcher.getDataField());
    }
  }, [data, dataFetcher]);

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
      await new Promise((resolve) => setTimeout(resolve, 100));

      // Start with current data point only (no fake historical data)
      const newData = await dataFetcher.generateInitialData(1);
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

  if (isLoading) {
    return (
      <div className='bg-white rounded-lg shadow-lg p-6'>
        <div className='flex flex-col items-center justify-center h-64 space-y-3'>
          <div className='w-8 h-8 border-2 border-green-500 border-t-transparent rounded-full animate-spin' />
          <div className='text-gray-600'>
            {data.length === 0 ? 'Fetching initial data...' : 'Loading chart...'}
          </div>
        </div>
      </div>
    );
  }

  if (data.length === 0 && !isLoading) {
    return (
      <div className='bg-white rounded-lg shadow-lg p-6'>
        <div className='flex flex-col items-center justify-center h-64 space-y-3'>
          <div className='text-gray-600'>Waiting for data...</div>
          <div className='text-sm text-gray-500'>
            Chart will appear once data starts flowing
          </div>
          {error && (
            <div className='text-red-500 text-sm mt-2'>
              Error: {error}
            </div>
          )}
        </div>
      </div>
    );
  }

  return (
    <div className='bg-white rounded-lg shadow-lg p-6'>
      <div className='flex flex-col sm:flex-row sm:justify-between sm:items-start gap-3 sm:gap-0 mb-4'>
        {title && (
          <div className='order-1'>
            <h2 className='text-sm text-gray-600'>{title}</h2>
          </div>
        )}
        {!title && <div className='hidden sm:block' />}
        <div className='flex flex-col gap-2 order-2'>
          {/* Chart Duration Selector */}
          {chartConfigs && selectedConfigKey && onConfigChange && (
            <div className='flex justify-start sm:justify-end'>
              <div className='flex items-center gap-2 text-sm'>
                <label className='text-gray-600 whitespace-nowrap'>Duration:</label>
                <div className='relative'>
                  <select
                    value={selectedConfigKey}
                    onChange={(e) => handleConfigChange((e.target as HTMLSelectElement).value)}
                    disabled={isConfigChanging}
                    className={`px-2 py-1 border border-gray-300 rounded text-gray-700 text-sm focus:outline-none focus:ring-2 focus:ring-blue-300 focus:border-transparent ${
                      isConfigChanging ? "opacity-50 cursor-not-allowed" : ""
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
                      <div className='w-3 h-3 border border-blue-500 border-t-transparent rounded-full animate-spin' />
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
         />
        <span className='text-sm text-gray-600'>
          {isConfigChanging ? "Updating configuration..." : isRunning ? "Live" : "Paused"} •{" "}
          {data.length} data points
          {useAreaChart && <span className='text-green-500 ml-2'>• Area Chart</span>}
          {dataAggregationSeconds > 1 && (
            <span className='text-purple-500 ml-2'>
              • Aggregated({formatInterval(dataAggregationSeconds)})
            </span>
          )}
          {error && <span className='text-red-500 ml-2'>• {error}</span>}
        </span>
      </div>

      <div className='relative'>
        {isConfigChanging && (
          <div className='absolute inset-0 bg-white/80 backdrop-blur-sm z-10 flex items-center justify-center rounded-lg'>
            <div className='flex items-center gap-3 text-gray-600'>
              <div className='w-5 h-5 border-2 border-blue-500 border-t-transparent rounded-full animate-spin' />
              <span className='text-sm font-medium'>Updating chart configuration...</span>
            </div>
          </div>
        )}
        <Chart
          data={data}
          seriesOptions={{ color }}
          useAreaChart={useAreaChart}
          dataAggregationSeconds={dataAggregationSeconds}
          lineStyle={lineStyle}
        />
      </div>
    </div>
  );
};

export default RealTimeApiChart;
