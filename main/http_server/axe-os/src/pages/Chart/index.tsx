import { useState } from "preact/hooks";
import Chart from "../../components/Chart/Chart";
import ChartDemo from "./ChartDemo";
import { INITIAL_DATA } from "../../components/Chart/config";

export default function ChartPage() {
  const [showRealTime, setShowRealTime] = useState(false);

  const priceLineOptions = {
    price: 25,
    color: "#2962FF",
    lineWidth: 2,
    lineStyle: 2,
    axisLabelVisible: true,
    title: "Average Price",
  };

  if (showRealTime) {
    return <ChartDemo onBack={() => setShowRealTime(false)} />;
  }

  return (
    <div className='p-4'>
      <div className='flex justify-between items-center mb-6'>
        <h1 className='text-2xl font-bold'>Chart Examples</h1>
        <button
          onClick={() => setShowRealTime(true)}
          className='px-4 py-2 bg-blue-500 hover:bg-blue-600 text-white rounded-md font-medium transition-colors'
        >
          View Real-Time Demo
        </button>
      </div>

      <div className='space-y-6'>
        <div className='bg-white rounded-lg shadow-lg p-6'>
          <h2 className='text-xl font-semibold mb-4'>Static Chart Example</h2>
          <p className='text-gray-600 mb-4'>
            Basic lightweight chart with static data and price line
          </p>
          <div className='border rounded p-4'>
            <Chart data={INITIAL_DATA} priceLineOptions={priceLineOptions} />
          </div>
        </div>

        <div className='bg-white rounded-lg shadow-lg p-6'>
          <h2 className='text-xl font-semibold mb-4'>Real-Time Features</h2>
          <div className='grid grid-cols-1 md:grid-cols-2 gap-4'>
            <div className='bg-blue-50 p-4 rounded-lg'>
              <h3 className='font-semibold text-blue-800 mb-2'>Live Data Updates</h3>
              <p className='text-sm text-blue-700'>
                Charts that update automatically with new data points in real-time
              </p>
            </div>
            <div className='bg-green-50 p-4 rounded-lg'>
              <h3 className='font-semibold text-green-800 mb-2'>Mock Data Generation</h3>
              <p className='text-sm text-green-700'>
                Realistic simulation of mining metrics with volatility and trends
              </p>
            </div>
            <div className='bg-purple-50 p-4 rounded-lg'>
              <h3 className='font-semibold text-purple-800 mb-2'>Interactive Controls</h3>
              <p className='text-sm text-purple-700'>
                Start/stop live updates and reset data with button controls
              </p>
            </div>
            <div className='bg-orange-50 p-4 rounded-lg'>
              <h3 className='font-semibold text-orange-800 mb-2'>Performance Optimized</h3>
              <p className='text-sm text-orange-700'>
                Efficient rendering with sliding window data management
              </p>
            </div>
          </div>

          <div className='mt-6 text-center'>
            <button
              onClick={() => setShowRealTime(true)}
              className='px-6 py-3 bg-gradient-to-r from-blue-500 to-purple-600 hover:from-blue-600 hover:to-purple-700 text-white rounded-lg font-medium transition-all transform hover:scale-105'
            >
              🚀 Launch Real-Time Dashboard
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
