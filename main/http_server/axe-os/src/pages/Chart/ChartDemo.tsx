import { useMemo } from "preact/hooks";
import RealTimeChart from "../../components/Chart/RealTimeChart";
import { createHashrateDataGenerator } from "../../utils/mockDataGenerator";

interface ChartDemoProps {
  onBack?: () => void;
}

const ChartDemo = ({ onBack }: ChartDemoProps) => {
  // Create a single data generator for demonstration
  const hashrateGenerator = useMemo(() => createHashrateDataGenerator(), []);

  return (
    <div className='min-h-screen bg-gray-100 p-6'>
      <div className='max-w-4xl mx-auto'>
        <div className='mb-8'>
          <div className='flex items-center justify-between mb-4'>
            <h1 className='text-3xl font-bold text-gray-900'>Real-Time Chart Demo</h1>
            {onBack && (
              <button
                onClick={onBack}
                className='px-4 py-2 bg-gray-500 hover:bg-gray-600 text-white rounded-md font-medium transition-colors'
              >
                ← Back to Charts
              </button>
            )}
          </div>
          <p className='text-gray-600'>
            This chart simulates real-time hashrate data with mock updates every 2 seconds
          </p>
        </div>

        {/* Single real-time chart */}
        <div className='mb-8'>
          <RealTimeChart
            title='Mining Hashrate (TH/s)'
            dataGenerator={hashrateGenerator}
            updateInterval={2000}
            maxDataPoints={50}
            color='#10b981'
          />
        </div>

        <div className='mt-8 bg-white rounded-lg shadow-lg p-6'>
          <h2 className='text-xl font-bold text-gray-800 mb-4'>How It Works</h2>
          <div className='space-y-4'>
            <div className='flex items-start space-x-3'>
              <div className='w-6 h-6 bg-green-500 rounded-full flex items-center justify-center text-white text-sm font-bold'>
                1
              </div>
              <div>
                <h3 className='font-semibold text-gray-800'>Mock Data Generation</h3>
                <p className='text-sm text-gray-600'>
                  The chart uses a MockDataGenerator class that creates realistic data with
                  volatility and trends
                </p>
              </div>
            </div>
            <div className='flex items-start space-x-3'>
              <div className='w-6 h-6 bg-blue-500 rounded-full flex items-center justify-center text-white text-sm font-bold'>
                2
              </div>
              <div>
                <h3 className='font-semibold text-gray-800'>Real-Time Updates</h3>
                <p className='text-sm text-gray-600'>
                  New data points are added every 2 seconds using setInterval when "Start Live
                  Updates" is clicked
                </p>
              </div>
            </div>
            <div className='flex items-start space-x-3'>
              <div className='w-6 h-6 bg-purple-500 rounded-full flex items-center justify-center text-white text-sm font-bold'>
                3
              </div>
              <div>
                <h3 className='font-semibold text-gray-800'>Sliding Window</h3>
                <p className='text-sm text-gray-600'>
                  Only the last 50 data points are kept to prevent memory issues and maintain
                  performance
                </p>
              </div>
            </div>
            <div className='flex items-start space-x-3'>
              <div className='w-6 h-6 bg-orange-500 rounded-full flex items-center justify-center text-white text-sm font-bold'>
                4
              </div>
              <div>
                <h3 className='font-semibold text-gray-800'>Chart Updates</h3>
                <p className='text-sm text-gray-600'>
                  The lightweight-charts library efficiently renders the new data and auto-scrolls
                  to show the latest values
                </p>
              </div>
            </div>
          </div>

          <div className='mt-6 p-4 bg-gray-50 rounded-lg'>
            <h4 className='font-semibold text-gray-800 mb-2'>Try the controls:</h4>
            <ul className='text-sm text-gray-600 space-y-1'>
              <li>• Click "Start Live Updates" to begin real-time data simulation</li>
              <li>• Click "Stop Live Updates" to pause the data stream</li>
              <li>• Click "Reset Data" to generate a fresh set of historical data</li>
              <li>• Watch the green indicator pulse when live updates are active</li>
            </ul>
          </div>
        </div>
      </div>
    </div>
  );
};

export default ChartDemo;
