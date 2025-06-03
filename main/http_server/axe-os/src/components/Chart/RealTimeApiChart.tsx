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
  // New props for better default noise reduction
  defaultSmoothingFactor?: number;
  defaultUseAreaChart?: boolean;
  defaultDataAggregation?: number;
  // Chart configuration props
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
  defaultSmoothingFactor = 5, // Increased from 3 to 5 for less noise
  defaultUseAreaChart = true, // Changed to true for better visual appeal
  defaultDataAggregation = 5, // Increased from 2 to 5 for better aggregation
  chartConfigs,
  selectedConfigKey,
  onConfigChange,
}: RealTimeApiChartProps) => {
  const [data, setData] = useState<ChartDataPoint[]>([]);
  const [isRunning, setIsRunning] = useState(false);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  // New state for noise reduction features
  const [smoothingFactor, setSmoothingFactor] = useState(defaultSmoothingFactor);
  const [useAreaChart, setUseAreaChart] = useState(defaultUseAreaChart);
  const [dataAggregationSeconds, setDataAggregationSeconds] = useState(defaultDataAggregation);
  const [showAdvancedControls, setShowAdvancedControls] = useState(false);
  const [isConfigChanging, setIsConfigChanging] = useState(false);

  const intervalRef = useRef<NodeJS.Timeout | null>(null);
  const controlsContentRef = useRef<HTMLDivElement>(null);
  const [controlsHeight, setControlsHeight] = useState(0);

  // Measure controls content height when it mounts
  useEffect(() => {
    if (controlsContentRef.current) {
      setControlsHeight(controlsContentRef.current.scrollHeight);
    }
  }, [showAdvancedControls]);

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

  // Cleanup interval on unmount
  useEffect(() => {
    return () => {
      if (intervalRef.current) {
        clearInterval(intervalRef.current);
      }
    };
  }, []);

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
          {/* Primary Controls Row */}
          <div className='flex gap-2 items-center justify-end'>
            <button
              onClick={toggleRealTime}
              disabled={isLoading || isConfigChanging}
              className={`px-3 py-1.5 rounded text-sm font-medium transition-colors disabled:opacity-50 ${
                isRunning
                  ? "bg-red-500 hover:bg-red-600 text-white"
                  : "bg-green-500 hover:bg-green-600 text-white"
              }`}
            >
              {isRunning ? "Stop" : "Start"} Live Updates
            </button>
            <button
              onClick={resetData}
              disabled={isLoading || isConfigChanging}
              className='px-3 py-1.5 border border-blue-500 text-blue-500 hover:bg-blue-50 rounded text-sm font-medium transition-colors disabled:opacity-50'
            >
              Reset Data
            </button>
          </div>

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
                    <option value="SHORT">30 minutes</option>
                    <option value="MEDIUM">1 hour</option>
                    <option value="LONG">2 hours</option>
                    <option value="EXTENDED">4 hours</option>
                    <option value="FULL_DAY">6 hours</option>
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

          {/* Advanced Controls Toggle */}
          <div className='flex justify-end'>
            <button
              onClick={() => setShowAdvancedControls(!showAdvancedControls)}
              className='text-sm text-gray-500 hover:text-gray-700 underline transition-colors flex items-center gap-1'
              aria-expanded={showAdvancedControls}
              aria-controls="noise-reduction-controls"
            >
              <span>{showAdvancedControls ? 'Hide' : 'Show'} Noise Reduction Controls</span>
              <svg
                className={`w-3 h-3 transition-transform duration-200 ${
                  showAdvancedControls ? 'rotate-180' : 'rotate-0'
                }`}
                fill="none"
                stroke="currentColor"
                viewBox="0 0 24 24"
              >
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 9l-7 7-7-7" />
              </svg>
            </button>
          </div>

          {/* Advanced Controls Panel with smooth transition */}
          <div
            id="noise-reduction-controls"
            className={`overflow-hidden transition-all duration-300 ease-in-out ${
              showAdvancedControls
                ? 'opacity-100'
                : 'opacity-0'
            }`}
            style={{
              height: showAdvancedControls ? `${controlsHeight}px` : '0px'
            }}
            aria-hidden={!showAdvancedControls}
          >
            <div
              ref={controlsContentRef}
              className='flex flex-wrap gap-3 p-3 bg-gray-50 rounded-lg text-sm'
            >
              <div className='flex items-center gap-2'>
                <label className='flex items-center gap-1'>
                  <input
                    type="checkbox"
                    checked={useAreaChart}
                    onChange={(e) => setUseAreaChart((e.target as HTMLInputElement).checked)}
                    className='rounded'
                  />
                  Area Chart
                </label>
              </div>

              <div className='flex items-center gap-2'>
                <label className='whitespace-nowrap'>Smoothing:</label>
                <select
                  value={smoothingFactor}
                  onChange={(e) => setSmoothingFactor(Number((e.target as HTMLSelectElement).value))}
                  className='px-2 py-1 border border-gray-300 rounded'
                >
                  <option value={1}>None</option>
                  <option value={3}>Light</option>
                  <option value={5}>Medium</option>
                  <option value={7}>Heavy</option>
                </select>
              </div>

              <div className='flex items-center gap-2'>
                <label className='whitespace-nowrap'>Data Aggregation:</label>
                <select
                  value={dataAggregationSeconds}
                  onChange={(e) => setDataAggregationSeconds(Number((e.target as HTMLSelectElement).value))}
                  className='px-2 py-1 border border-gray-300 rounded'
                >
                  <option value={1}>None</option>
                  <option value={2}>2s</option>
                  <option value={5}>5s</option>
                  <option value={10}>10s</option>
                </select>
              </div>
            </div>
          </div>
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
          {smoothingFactor > 1 && <span className='text-blue-500 ml-2'>• Smoothed({smoothingFactor})</span>}
          {dataAggregationSeconds > 1 && <span className='text-purple-500 ml-2'>• Aggregated({dataAggregationSeconds}s)</span>}
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
          smoothingFactor={smoothingFactor}
          useAreaChart={useAreaChart}
          dataAggregationSeconds={dataAggregationSeconds}
        />
      </div>
    </div>
  );
};

export default RealTimeApiChart;
