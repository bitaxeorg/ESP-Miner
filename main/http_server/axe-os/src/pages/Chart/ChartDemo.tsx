import { useMemo } from "preact/hooks";
import RealTimeApiChart from "../../components/Chart/RealTimeApiChart";
import {
  createHashrateDataFetcher,
  createTemperatureDataFetcher,
  createPowerDataFetcher,
} from "../../utils/realTimeDataFetcher";
import {
  CHART_CONFIGS,
  calculateChartMemory,
  formatMemorySize,
  validateChartConfig
} from "../../utils/chartMemoryUtils";

const ChartDemo = () => {
  // Create data fetchers for different metrics
  const hashrateDataFetcher = useMemo(() => createHashrateDataFetcher(), []);
  const temperatureDataFetcher = useMemo(() => createTemperatureDataFetcher(), []);
  const powerDataFetcher = useMemo(() => createPowerDataFetcher(), []);

  // Use the FULL_DAY configuration (6 hours) as requested
  const chartConfig = CHART_CONFIGS.FULL_DAY;
  const memoryInfo = calculateChartMemory(chartConfig.dataPoints);
  const configValidation = validateChartConfig(chartConfig);

  return (
    <div className='min-h-screen bg-gray-100 p-6'>
      <div className='max-w-6xl mx-auto'>
        <div className='mb-8'>
          <div className='flex items-center justify-between mb-4'>
            <h1 className='text-3xl font-bold text-gray-900'>Real-Time Mining Data</h1>
            <div className='text-right text-sm text-gray-500'>
              <div>Memory usage: {formatMemorySize(memoryInfo.estimatedMemoryKB)}</div>
              <div>{chartConfig.dataPoints} data points • {chartConfig.description}</div>
            </div>
          </div>
          <p className='text-gray-600 mb-4'>
            Live data from your mining device, updated every 5 seconds ({chartConfig.description})
          </p>

          {/* Memory and Performance Info */}
          <div className='bg-blue-50 rounded-lg p-4 mb-6'>
            <h3 className='text-sm font-semibold text-blue-800 mb-2'>System Information</h3>
            <div className='grid grid-cols-1 md:grid-cols-3 gap-4 text-sm text-blue-700'>
              <div>
                <span className='font-medium'>Data Storage:</span> In-memory (RAM)
              </div>
              <div>
                <span className='font-medium'>Chart Library:</span> TradingView Lightweight Charts
              </div>
              <div>
                <span className='font-medium'>Performance:</span> Optimized for real-time updates
              </div>
            </div>

            {configValidation.warnings.length > 0 && (
              <div className='mt-3 p-2 bg-yellow-100 rounded border border-yellow-200'>
                <div className='text-sm font-medium text-yellow-800'>Recommendations:</div>
                <ul className='text-sm text-yellow-700 mt-1 list-disc ml-5'>
                  {configValidation.warnings.map((warning, index) => (
                    <li key={index}>{warning}</li>
                  ))}
                </ul>
              </div>
            )}
          </div>
        </div>

        {/* Real-time charts - all full width */}
        <div className='space-y-6 mb-8'>
          <RealTimeApiChart
            title='Mining Hashrate'
            dataFetcher={hashrateDataFetcher}
            updateInterval={chartConfig.intervalSeconds * 1000}
            maxDataPoints={chartConfig.dataPoints}
            color='#10b981'
            unit='GH/s'
          />

          {/* <RealTimeApiChart
            title='Temperature'
            dataFetcher={temperatureDataFetcher}
            updateInterval={chartConfig.intervalSeconds * 1000}
            maxDataPoints={chartConfig.dataPoints}
            color='#f59e0b'
            unit='°C'
          />

          <RealTimeApiChart
            title='Power Consumption'
            dataFetcher={powerDataFetcher}
            updateInterval={chartConfig.intervalSeconds * 1000}
            maxDataPoints={chartConfig.dataPoints}
            color='#ef4444'
            unit='W'
          /> */}
        </div>
      </div>
    </div>
  );
};

export default ChartDemo;
