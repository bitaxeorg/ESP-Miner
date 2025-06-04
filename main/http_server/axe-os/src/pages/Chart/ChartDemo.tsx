import { useMemo, useState } from "preact/hooks";
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
  validateChartConfig,
  ChartTimeConfig
} from "../../utils/chartMemoryUtils";

const ChartDemo = () => {
  // Create data fetchers for different metrics
  const hashrateDataFetcher = useMemo(() => createHashrateDataFetcher(), []);
  const temperatureDataFetcher = useMemo(() => createTemperatureDataFetcher(), []);
  const powerDataFetcher = useMemo(() => createPowerDataFetcher(), []);

  // State for selected chart configuration
  const [selectedConfigKey, setSelectedConfigKey] = useState<string>('FULL_DAY');

  // Current configuration: 6 hours of data (4,320 data points at 5-second intervals)
  // Available options:
  // - CHART_CONFIGS.SHORT: 30 minutes (360 points)
  // - CHART_CONFIGS.MEDIUM: 1 hour (720 points)
  // - CHART_CONFIGS.LONG: 2 hours (1,440 points)
  // - CHART_CONFIGS.EXTENDED: 4 hours (2,880 points)
  // - CHART_CONFIGS.FULL_DAY: 6 hours (4,320 points) ← Default selection
  const chartConfig = CHART_CONFIGS[selectedConfigKey];
  const memoryInfo = calculateChartMemory(chartConfig.dataPoints);
  const configValidation = validateChartConfig(chartConfig);

  return (
    <div className='min-h-screen bg-gradient-to-br from-slate-50 to-gray-100'>
      {/* Header Section with Gradient */}
      <div className='bg-gradient-to-r from-blue-600 via-blue-700 to-indigo-800 text-white'>
        <div className='max-w-7xl text-left px-6 py-12'>
          <div className='flex flex-col lg:flex-row justify-between items-start lg:items-center gap-6'>
            <div>
              <h1 className='text-4xl lg:text-5xl font-bold mb-4'>
                Real-Time Mining Dashboard
              </h1>
              <p className='text-xl text-blue-100 max-w-2xl'>
                Monitor your mining performance with live data updates every 5 seconds.
                Advanced analytics and noise reduction controls included.
              </p>
            </div>

            {/* Performance Stats Card - simplified without Duration Selector */}
            <div className='bg-white/10 backdrop-blur-sm rounded-xl p-6 border border-white/20 min-w-[320px]'>
              <div className='flex items-center justify-between mb-4'>
                <h3 className='text-lg font-semibold text-blue-100'>Chart Settings</h3>
              </div>

              <div className='space-y-2 text-sm'>
                <div className='flex justify-between'>
                  <span className='text-blue-200'>Duration:</span>
                  <span className='font-medium'>{chartConfig.description}</span>
                </div>
                <div className='flex justify-between'>
                  <span className='text-blue-200'>Data Points:</span>
                  <span className='font-medium'>{chartConfig.dataPoints.toLocaleString()}</span>
                </div>
                <div className='flex justify-between'>
                  <span className='text-blue-200'>Memory Usage:</span>
                  <span className='font-medium'>{formatMemorySize(memoryInfo.estimatedMemoryKB)}</span>
                </div>
                <div className='flex justify-between'>
                  <span className='text-blue-200'>Update Rate:</span>
                  <span className='font-medium'>Every {chartConfig.intervalSeconds}s</span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* Main Content */}
      <div className='max-w-7xl mx-auto px-6 py-8'>
        {/* Technology Stack Cards */}
        <div className='grid grid-cols-1 md:grid-cols-3 gap-6 mb-8'>
          <div className='bg-white rounded-xl shadow-sm border border-gray-200 p-6'>
            <div className='flex items-center gap-3 mb-3'>
              <div className='w-10 h-10 bg-green-100 rounded-lg flex items-center justify-center'>
                <svg className='w-6 h-6 text-green-600' fill='none' stroke='currentColor' viewBox='0 0 24 24'>
                  <path strokeLinecap='round' strokeLinejoin='round' strokeWidth={2} d='M13 10V3L4 14h7v7l9-11h-7z' />
                </svg>
              </div>
              <div>
                <h3 className='font-semibold text-gray-900'>Real-Time Data</h3>
                <p className='text-sm text-gray-500'>Live updates every 5s</p>
              </div>
            </div>
            <p className='text-sm text-gray-600'>
              Streaming data directly from your mining hardware with minimal latency.
            </p>
          </div>

          <div className='bg-white rounded-xl shadow-sm border border-gray-200 p-6'>
            <div className='flex items-center gap-3 mb-3'>
              <div className='w-10 h-10 bg-blue-100 rounded-lg flex items-center justify-center'>
                <svg className='w-6 h-6 text-blue-600' fill='none' stroke='currentColor' viewBox='0 0 24 24'>
                  <path strokeLinecap='round' strokeLinejoin='round' strokeWidth={2} d='M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z' />
                </svg>
              </div>
              <div>
                <h3 className='font-semibold text-gray-900'>Advanced Charts</h3>
                <p className='text-sm text-gray-500'>TradingView engine</p>
              </div>
            </div>
            <p className='text-sm text-gray-600'>
              Professional-grade charting with smoothing and noise reduction features.
            </p>
          </div>

          <div className='bg-white rounded-xl shadow-sm border border-gray-200 p-6'>
            <div className='flex items-center gap-3 mb-3'>
              <div className='w-10 h-10 bg-purple-100 rounded-lg flex items-center justify-center'>
                <svg className='w-6 h-6 text-purple-600' fill='none' stroke='currentColor' viewBox='0 0 24 24'>
                  <path strokeLinecap='round' strokeLinejoin='round' strokeWidth={2} d='M4.318 6.318a4.5 4.5 0 000 6.364L12 20.364l7.682-7.682a4.5 4.5 0 00-6.364-6.364L12 7.636l-1.318-1.318a4.5 4.5 0 00-6.364 0z' />
                </svg>
              </div>
              <div>
                <h3 className='font-semibold text-gray-900'>Optimized Performance</h3>
                <p className='text-sm text-gray-500'>Memory efficient</p>
              </div>
            </div>
            <p className='text-sm text-gray-600'>
              In-memory data storage with automatic cleanup and performance monitoring.
            </p>
          </div>
        </div>

        {/* Performance Recommendations */}
        {configValidation.warnings.length > 0 && (
          <div className='bg-amber-50 border border-amber-200 rounded-xl p-6 mb-8'>
            <div className='flex items-start gap-3'>
              <div className='w-8 h-8 bg-amber-100 rounded-lg flex items-center justify-center flex-shrink-0 mt-0.5'>
                <svg className='w-5 h-5 text-amber-600' fill='none' stroke='currentColor' viewBox='0 0 24 24'>
                  <path strokeLinecap='round' strokeLinejoin='round' strokeWidth={2} d='M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z' />
                </svg>
              </div>
              <div>
                <h3 className='font-semibold text-amber-800 mb-2'>Performance Recommendations</h3>
                <ul className='space-y-1 text-sm text-amber-700'>
                  {configValidation.warnings.map((warning, index) => (
                    <li key={index} className='flex items-start gap-2'>
                      <span className='text-amber-500 mt-1'>•</span>
                      <span>{warning}</span>
                    </li>
                  ))}
                </ul>
              </div>
            </div>
          </div>
        )}

        {/* Charts Section */}
        <div className='space-y-8'>
          <RealTimeApiChart
            title='Mining Hashrate'
            dataFetcher={hashrateDataFetcher}
            updateInterval={chartConfig.intervalSeconds * 1000}
            maxDataPoints={chartConfig.dataPoints}
            color='#10b981'
            unit='GH/s'
            defaultUseAreaChart={true}
            defaultDataAggregation={5}
            chartConfigs={CHART_CONFIGS}
            selectedConfigKey={selectedConfigKey}
            onConfigChange={setSelectedConfigKey}
          />

          {/* Placeholder for future charts */}
          {/*
          <RealTimeApiChart
            title='Temperature'
            dataFetcher={temperatureDataFetcher}
            updateInterval={chartConfig.intervalSeconds * 1000}
            maxDataPoints={chartConfig.dataPoints}
            color='#f59e0b'
            unit='°C'
            defaultSmoothingFactor={5}
            defaultUseAreaChart={true}
            defaultDataAggregation={5}
          />

          <RealTimeApiChart
            title='Power Consumption'
            dataFetcher={powerDataFetcher}
            updateInterval={chartConfig.intervalSeconds * 1000}
            maxDataPoints={chartConfig.dataPoints}
            color='#ef4444'
            unit='W'
            defaultSmoothingFactor={5}
            defaultUseAreaChart={true}
            defaultDataAggregation={5}
          />
          */}
        </div>
      </div>
    </div>
  );
};

export default ChartDemo;
