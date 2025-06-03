import { useMemo } from "preact/hooks";
import RealTimeApiChart from "../../components/Chart/RealTimeApiChart";
import {
  createHashrateDataFetcher,
  createTemperatureDataFetcher,
  createPowerDataFetcher,
} from "../../utils/realTimeDataFetcher";

const ChartDemo = () => {
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
          </div>
          <p className='text-gray-600'>
            Live data from your mining device, updated every 2 seconds
          </p>
        </div>

        {/* Real-time charts - all full width */}
        <div className='space-y-6 mb-8'>
          <RealTimeApiChart
            title='Mining Hashrate'
            dataFetcher={hashrateDataFetcher}
            updateInterval={2000}
            maxDataPoints={50}
            color='#10b981'
            unit='TH/s'
          />

          {/* <RealTimeApiChart
            title='Temperature'
            dataFetcher={temperatureDataFetcher}
            updateInterval={2000}
            maxDataPoints={50}
            color='#f59e0b'
            unit='°C'
          />

          <RealTimeApiChart
            title='Power Consumption'
            dataFetcher={powerDataFetcher}
            updateInterval={2000}
            maxDataPoints={50}
            color='#ef4444'
            unit='W'
          /> */}
        </div>
      </div>
    </div>
  );
};

export default ChartDemo;
