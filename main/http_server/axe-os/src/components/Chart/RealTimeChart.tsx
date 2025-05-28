import { useEffect, useState, useRef } from "preact/hooks";
import Chart from "./Chart";
import { ChartDataPoint } from "./config";
import { MockDataGenerator } from "../../utils/mockDataGenerator";

interface RealTimeChartProps {
  title: string;
  dataGenerator: MockDataGenerator;
  updateInterval?: number;
  maxDataPoints?: number;
  color?: string;
}

const RealTimeChart = ({
  title,
  dataGenerator,
  updateInterval = 1000,
  maxDataPoints = 50,
  color = "#2563eb",
}: RealTimeChartProps) => {
  const [data, setData] = useState<ChartDataPoint[]>([]);
  const [isRunning, setIsRunning] = useState(false);
  const intervalRef = useRef<NodeJS.Timeout | null>(null);

  // Initialize with some historical data
  useEffect(() => {
    const initialData = dataGenerator.generateInitialData(maxDataPoints);
    setData(initialData);
  }, [dataGenerator, maxDataPoints]);

  // Start/stop real-time updates
  const toggleRealTime = () => {
    if (isRunning) {
      if (intervalRef.current) {
        clearInterval(intervalRef.current);
        intervalRef.current = null;
      }
      setIsRunning(false);
    } else {
      intervalRef.current = setInterval(() => {
        const newPoint = dataGenerator.generateNextPoint();

        setData((prevData) => {
          const newData = [...prevData, newPoint];
          // Keep only the last maxDataPoints
          return newData.slice(-maxDataPoints);
        });
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

  const currentValue = data.length > 0 ? data[data.length - 1].value : 0;

  return (
    <div className='bg-white rounded-lg shadow-lg p-6'>
      <div className='flex justify-between items-center mb-4'>
        <div>
          <h2 className='text-xl font-bold text-gray-800'>{title}</h2>
          <p className='text-2xl font-semibold' style={{ color }}>
            {currentValue.toFixed(2)}
          </p>
        </div>
        <div className='flex gap-2'>
          <button
            onClick={toggleRealTime}
            className={`px-4 py-2 rounded-md font-medium transition-colors ${
              isRunning
                ? "bg-red-500 hover:bg-red-600 text-white"
                : "bg-green-500 hover:bg-green-600 text-white"
            }`}
          >
            {isRunning ? "Stop" : "Start"} Live Updates
          </button>
          <button
            onClick={() => {
              const newData = dataGenerator.generateInitialData(maxDataPoints);
              setData(newData);
            }}
            className='px-4 py-2 bg-blue-500 hover:bg-blue-600 text-white rounded-md font-medium transition-colors'
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
        </span>
      </div>

      <div className='relative'>
        <Chart data={data} seriesOptions={{ color }} />
      </div>
    </div>
  );
};

export default RealTimeChart;
