import {
  createChart,
  IChartApi,
  ISeriesApi,
  LineSeries,
  LineSeriesOptions,
  LineType,
} from "lightweight-charts";
import { useEffect, useRef, useState } from "preact/hooks";
import { chartOption, ChartDataPoint } from "./config";

interface ChartProps {
  data: ChartDataPoint[];
  priceLineOptions?: any;
  seriesOptions?: Partial<LineSeriesOptions>;
  title?: string;
  [key: string]: any;
}

const Chart = ({ data, priceLineOptions, seriesOptions, title, ...rest }: ChartProps) => {
  const chartRef = useRef<HTMLDivElement>(null);
  const chartInstanceRef = useRef<IChartApi | null>(null);
  const seriesRef = useRef<ISeriesApi<"Line"> | null>(null);

  useEffect(() => {
    if (!chartRef.current) return;

    // Create chart instance with explicit width
    const chart = createChart(chartRef.current, {
      ...chartOption,
      width: chartRef.current.clientWidth,
      height: 400,
    });
    chartInstanceRef.current = chart;

    // Add line series
    const lineSeries = chart.addSeries(LineSeries, {
      color: "#2563eb",
      lineWidth: 2,
      lineType: LineType.Curved,
      ...seriesOptions,
    });
    seriesRef.current = lineSeries;

    // Set initial data
    lineSeries.setData(data);

    // Add price line if provided
    if (priceLineOptions) {
      lineSeries.createPriceLine(priceLineOptions);
    }

    // Fit content to show all data
    chart.timeScale().fitContent();

    // Handle resize
    const handleResize = () => {
      if (chartRef.current && chartInstanceRef.current) {
        chartInstanceRef.current.applyOptions({
          width: chartRef.current.clientWidth,
        });
      }
    };

    // Add resize listener
    window.addEventListener('resize', handleResize);

    // Use ResizeObserver for container size changes
    let resizeObserver: ResizeObserver | null = null;
    if (window.ResizeObserver) {
      resizeObserver = new ResizeObserver(() => {
        handleResize();
      });
      resizeObserver.observe(chartRef.current);
    }

    // Cleanup function
    return () => {
      window.removeEventListener('resize', handleResize);
      if (resizeObserver) {
        resizeObserver.disconnect();
      }
      chart.remove();
      chartInstanceRef.current = null;
      seriesRef.current = null;
    };
  }, []);

  // Update data when it changes
  useEffect(() => {
    if (seriesRef.current && data.length > 0) {
      seriesRef.current.setData(data);

      // Auto-scroll to show latest data
      if (chartInstanceRef.current) {
        chartInstanceRef.current.timeScale().scrollToRealTime();
      }
    }
  }, [data]);

  return (
    <div className='w-full'>
      {title && <h3 className='text-lg font-semibold mb-2 text-gray-800'>{title}</h3>}
      <div
        style={{
          height: "400px",
          position: "relative",
          width: "100%",
        }}
        ref={chartRef}
        {...rest}
      />
    </div>
  );
};

export default Chart;
