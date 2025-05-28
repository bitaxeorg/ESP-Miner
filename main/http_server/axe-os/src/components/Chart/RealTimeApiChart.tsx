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
}

const RealTimeApiChart = ({
  title,
  dataFetcher,
  updateInterval = 2000, // Default to 2 seconds for API calls
  maxDataPoints = 50,
  color = "#2563eb",
  unit = "",
}: RealTimeApiChartProps) => {
  const [data, setData] = useState<ChartDataPoint[]>([]);
  const [isRunning, setIsRunning] = useState(false);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
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
      <div className='flex justify-between items-center mb-4'>
        <div>
          <h2 className='text-xl font-bold text-gray-800'>{title}</h2>
          <p className='text-2xl font-semibold' style={{ color }}>
            {currentValue.toFixed(2)} {unit}
          </p>
        </div>
        <div className='flex gap-2'>
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
      </div>

      <div className='mb-4'>
        <span
          className={`inline-block w-3 h-3 rounded-full mr-2 ${
            isRunning ? "bg-green-500 animate-pulse" : "bg-gray-400"
          }`}
        ></span>
        <span className='text-sm text-gray-600'>
          {isRunning ? "Live" : "Paused"} • {data.length} data points
          {error && <span className='text-red-500 ml-2'>• {error}</span>}
        </span>
      </div>

      <div className='relative'>
        <Chart data={data} seriesOptions={{ color }} />
      </div>
    </div>
  );
};

export default RealTimeApiChart;
