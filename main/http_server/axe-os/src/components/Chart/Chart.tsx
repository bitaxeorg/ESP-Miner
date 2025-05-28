import {
  createChart,
  IChartApi,
  ISeriesApi,
  LineSeries,
  LineSeriesOptions,
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

    // Create chart instance
    const chart = createChart(chartRef.current, chartOption);
    chartInstanceRef.current = chart;

    // Add line series
    const lineSeries = chart.addSeries(LineSeries, {
      color: "#2563eb",
      lineWidth: 2,
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

    // Cleanup function
    return () => {
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
