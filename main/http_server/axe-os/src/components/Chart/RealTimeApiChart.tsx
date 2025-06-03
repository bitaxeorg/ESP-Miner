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
}

const RealTimeApiChart = ({
  title,
  dataFetcher,
  updateInterval = 2000, // Default to 2 seconds for API calls
  maxDataPoints = 50,
  color = "#10b981", // Updated to match the new chart color
  unit = "",
  defaultSmoothingFactor = 5, // Increased from 3 to 5 for less noise
  defaultUseAreaChart = false,
  defaultDataAggregation = 5, // Increased from 2 to 5 for better aggregation
}: RealTimeApiChartProps) => {
  const [data, setData] = useState<ChartDataPoint[]>([]);
  const [isRunning, setIsRunning] = useState(false);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [showMovingAverage, setShowMovingAverage] = useState(false);
  const [movingAveragePeriod, setMovingAveragePeriod] = useState(10);

  // New state for noise reduction features
  const [smoothingFactor, setSmoothingFactor] = useState(defaultSmoothingFactor);
  const [useAreaChart, setUseAreaChart] = useState(defaultUseAreaChart);
  const [dataAggregationSeconds, setDataAggregationSeconds] = useState(defaultDataAggregation);
  const [showAdvancedControls, setShowAdvancedControls] = useState(false);

  const intervalRef = useRef<NodeJS.Timeout | null>(null);

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
          <div className='flex gap-2 items-center'>
            <div className='flex items-center gap-2 mr-4'>
              <label className='flex items-center gap-2 text-sm'>
                <input
                  type="checkbox"
                  checked={showMovingAverage}
                  onChange={(e) => setShowMovingAverage((e.target as HTMLInputElement).checked)}
                  className='rounded'
                />
                Moving Average
              </label>
              {showMovingAverage && (
                <select
                  value={movingAveragePeriod}
                  onChange={(e) => setMovingAveragePeriod(Number((e.target as HTMLSelectElement).value))}
                  className='px-2 py-1 border border-gray-300 rounded text-sm'
                >
                  <option value={5}>5</option>
                  <option value={10}>10</option>
                  <option value={20}>20</option>
                  <option value={50}>50</option>
                </select>
              )}
            </div>
            <button
              onClick={toggleRealTime}
              disabled={isLoading}
              className={`px-4 py-2 rounded-md font-medium transition-colors disabled:opacity-50 ${
                isRunning
                  ? "bg-red-500 hover:bg-red-600 text-white"
                  : "bg-green-500 hover:bg-green-600 text-white"
              }`}
            >
              {isRunning ? "Stop" : "Start"} Live Updates
            </button>
            <button
              onClick={resetData}
              disabled={isLoading}
              className='px-4 py-2 bg-blue-500 hover:bg-blue-600 text-white rounded-md font-medium transition-colors disabled:opacity-50'
            >
              Reset Data
            </button>
          </div>

          {/* Advanced Controls Toggle */}
          <div className='flex justify-end'>
            <button
              onClick={() => setShowAdvancedControls(!showAdvancedControls)}
              className='text-sm text-gray-500 hover:text-gray-700 underline'
            >
              {showAdvancedControls ? 'Hide' : 'Show'} Noise Reduction Controls
            </button>
          </div>

          {/* Advanced Controls Panel */}
          {showAdvancedControls && (
            <div className='flex flex-wrap gap-3 p-3 bg-gray-50 rounded-lg text-sm'>
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
          )}
        </div>
      </div>

      <div className='mb-4'>
        <span
          className={`inline-block w-3 h-3 rounded-full mr-2 ${
            isRunning ? "bg-green-500 animate-pulse" : "bg-gray-400"
          }`}
        ></span>
        <span className='text-sm text-gray-600'>
          {isRunning ? "Live" : "Paused"} • {data.length} data points
          {showMovingAverage && <span className='text-slate-500 ml-2'>• MA({movingAveragePeriod})</span>}
          {smoothingFactor > 1 && <span className='text-blue-500 ml-2'>• Smoothed({smoothingFactor})</span>}
          {dataAggregationSeconds > 1 && <span className='text-purple-500 ml-2'>• Aggregated({dataAggregationSeconds}s)</span>}
          {error && <span className='text-red-500 ml-2'>• {error}</span>}
        </span>
      </div>

      <div className='relative'>
        <Chart
          data={data}
          seriesOptions={{ color }}
          showMovingAverage={showMovingAverage}
          movingAveragePeriod={movingAveragePeriod}
          smoothingFactor={smoothingFactor}
          useAreaChart={useAreaChart}
          dataAggregationSeconds={dataAggregationSeconds}
        />
      </div>
    </div>
  );
};

export default RealTimeApiChart;
