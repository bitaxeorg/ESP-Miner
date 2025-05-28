import { useMemo } from "preact/hooks";
import RealTimeApiChart from "../../components/Chart/RealTimeApiChart";
import {
  createHashrateDataFetcher,
  createTemperatureDataFetcher,
  createPowerDataFetcher,
} from "../../utils/realTimeDataFetcher";

interface ChartDemoProps {
  onBack?: () => void;
}

const ChartDemo = ({ onBack }: ChartDemoProps) => {
  // Create data fetchers for different metrics
  const hashrateDataFetcher = useMemo(() => createHashrateDataFetcher(), []);
  const temperatureDataFetcher = useMemo(() => createTemperatureDataFetcher(), []);
  const powerDataFetcher = useMemo(() => createPowerDataFetcher(), []);

  return (
    <div className='min-h-screen bg-gray-100 p-6'>
      <div className='max-w-6xl mx-auto'>
        <div className='mb-8'>
          <div className='flex items-center justify-between mb-4'>
            <h1 className='text-3xl font-bold text-gray-900'>Real-Time Mining Data</h1>
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
            Live data from your mining device, updated every 2 seconds
          </p>
        </div>

        {/* Real-time charts grid */}
        <div className='grid grid-cols-1 lg:grid-cols-2 gap-6 mb-8'>
          <RealTimeApiChart
            title='Mining Hashrate'
            dataFetcher={hashrateDataFetcher}
            updateInterval={2000}
            maxDataPoints={50}
            color='#10b981'
            unit='TH/s'
          />

          <RealTimeApiChart
            title='Temperature'
            dataFetcher={temperatureDataFetcher}
            updateInterval={2000}
            maxDataPoints={50}
            color='#f59e0b'
            unit='°C'
          />
        </div>

        <div className='grid grid-cols-1 mb-8'>
          <RealTimeApiChart
            title='Power Consumption'
            dataFetcher={powerDataFetcher}
            updateInterval={2000}
            maxDataPoints={50}
            color='#ef4444'
            unit='W'
          />
        </div>

        <div className='mt-8 bg-white rounded-lg shadow-lg p-6'>
          <h2 className='text-xl font-bold text-gray-800 mb-4'>Live Mining Data</h2>
          <div className='space-y-4'>
            <div className='flex items-start space-x-3'>
              <div className='w-6 h-6 bg-green-500 rounded-full flex items-center justify-center text-white text-sm font-bold'>
                1
              </div>
              <div>
                <h3 className='font-semibold text-gray-800'>Real API Data</h3>
                <p className='text-sm text-gray-600'>
                  Charts now display actual data from your mining device via the /api/system/info
                  endpoint
                </p>
              </div>
            </div>
            <div className='flex items-start space-x-3'>
              <div className='w-6 h-6 bg-blue-500 rounded-full flex items-center justify-center text-white text-sm font-bold'>
                2
              </div>
              <div>
                <h3 className='font-semibold text-gray-800'>Live Updates</h3>
                <p className='text-sm text-gray-600'>
                  Data is fetched every 2 seconds when live updates are enabled
                </p>
              </div>
            </div>
            <div className='flex items-start space-x-3'>
              <div className='w-6 h-6 bg-purple-500 rounded-full flex items-center justify-center text-white text-sm font-bold'>
                3
              </div>
              <div>
                <h3 className='font-semibold text-gray-800'>Multiple Metrics</h3>
                <p className='text-sm text-gray-600'>
                  Monitor hashrate, temperature, power consumption, and other key metrics in
                  real-time
                </p>
              </div>
            </div>
            <div className='flex items-start space-x-3'>
              <div className='w-6 h-6 bg-orange-500 rounded-full flex items-center justify-center text-white text-sm font-bold'>
                4
              </div>
              <div>
                <h3 className='font-semibold text-gray-800'>Error Handling</h3>
                <p className='text-sm text-gray-600'>
                  Graceful handling of API errors with user feedback and automatic retry
                </p>
              </div>
            </div>
          </div>

          <div className='mt-6 p-4 bg-gray-50 rounded-lg'>
            <h4 className='font-semibold text-gray-800 mb-2'>Available metrics:</h4>
            <ul className='text-sm text-gray-600 space-y-1'>
              <li>
                • <strong>Hashrate:</strong> Current mining hashrate in TH/s
              </li>
              <li>
                • <strong>Temperature:</strong> Device temperature in °C
              </li>
              <li>
                • <strong>Power:</strong> Power consumption in Watts
              </li>
              <li>
                • <strong>Voltage:</strong> Operating voltage
              </li>
              <li>
                • <strong>Current:</strong> Current draw in Amps
              </li>
              <li>
                • <strong>Fan RPM:</strong> Cooling fan speed
              </li>
              <li>
                • <strong>Frequency:</strong> ASIC operating frequency
              </li>
            </ul>
          </div>
        </div>
      </div>
    </div>
  );
};

export default ChartDemo;
