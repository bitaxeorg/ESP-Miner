import {
  createChart,
  IChartApi,
  ISeriesApi,
  LineSeries,
  LineSeriesOptions,
  LineType,
  LineStyle,
  AreaSeries,
  Time,
} from "lightweight-charts";
import { useEffect, useRef } from "preact/hooks";
import { chartOption, ChartDataPoint } from "./config";

interface ChartProps {
  data: ChartDataPoint[];
  priceLineOptions?: any;
  seriesOptions?: Partial<LineSeriesOptions>;
  title?: string;
  movingAveragePeriod?: number;
  showMovingAverage?: boolean;
  useAreaChart?: boolean;
  dataAggregationSeconds?: number;
  lineStyle?: 'solid' | 'dotted' | 'dashed';
  [key: string]: any;
}

const Chart = ({
  data,
  priceLineOptions,
  seriesOptions,
  title,
  movingAveragePeriod = 20,
  showMovingAverage = false,
  useAreaChart = false,
  dataAggregationSeconds = 5,
  lineStyle = 'solid',
  ...rest
}: ChartProps) => {
  const chartRef = useRef<HTMLDivElement>(null);
  const chartInstanceRef = useRef<IChartApi | null>(null);
  const seriesRef = useRef<ISeriesApi<"Line"> | ISeriesApi<"Area"> | null>(null);
  const maSeriesRef = useRef<ISeriesApi<"Line"> | null>(null);

  // Data aggregation function to reduce noise by averaging over time periods
  const aggregateData = (sourceData: ChartDataPoint[], seconds: number): ChartDataPoint[] => {
    if (seconds <= 1 || sourceData.length === 0) return sourceData;

    const aggregatedData: ChartDataPoint[] = [];
    const buckets = new Map<number, { sum: number; count: number; time: Time }>();

    sourceData.forEach(point => {
      // Handle both timestamp numbers and BusinessDay objects
      let timeValue: number;
      if (typeof point.time === 'number') {
        timeValue = point.time;
      } else {
        // For BusinessDay, convert to timestamp (rough approximation)
        const businessDay = point.time as any;
        const date = new Date(businessDay.year, businessDay.month - 1, businessDay.day);
        timeValue = Math.floor(date.getTime() / 1000);
      }

      const bucketKey = Math.floor(timeValue / seconds) * seconds;

      const existing = buckets.get(bucketKey);
      if (existing) {
        existing.sum += point.value;
        existing.count += 1;
      } else {
        buckets.set(bucketKey, { sum: point.value, count: 1, time: bucketKey as Time });
      }
    });

    // Convert buckets back to data points
    Array.from(buckets.values())
      .sort((a, b) => {
        const aTime = typeof a.time === 'number' ? a.time : 0;
        const bTime = typeof b.time === 'number' ? b.time : 0;
        return aTime - bTime;
      })
      .forEach(bucket => {
        aggregatedData.push({
          time: bucket.time,
          value: bucket.sum / bucket.count
        });
      });

    return aggregatedData;
  };

  // Calculate moving average data
  const calculateMovingAverageData = (sourceData: ChartDataPoint[], period: number): ChartDataPoint[] => {
    const maData: ChartDataPoint[] = [];

    for (let i = 0; i < sourceData.length; i++) {
      if (i < period - 1) {
        // Provide whitespace data points until the MA can be calculated
        maData.push({ time: sourceData[i].time, value: 0 });
      } else {
        // Calculate the moving average
        let sum = 0;
        for (let j = 0; j < period; j++) {
          sum += sourceData[i - j].value;
        }
        const maValue = sum / period;
        maData.push({ time: sourceData[i].time, value: maValue });
      }
    }

    return maData;
  };

  useEffect(() => {
    if (!chartRef.current) return;

    // Create chart instance with enhanced options for noise reduction
    const chart = createChart(chartRef.current, {
      ...chartOption,
      width: chartRef.current.clientWidth,
      height: 400,
      // Enhanced chart options for smoother appearance
      layout: {
        ...chartOption.layout,
        fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif',
      },
      rightPriceScale: {
        ...chartOption.rightPriceScale,
        autoScale: true,
        scaleMargins: {
          top: 0.1,
          bottom: 0.1,
        },
      },
      timeScale: {
        ...chartOption.timeScale,
        fixLeftEdge: false,
        fixRightEdge: true,
        lockVisibleTimeRangeOnResize: true,
      },
    });
    chartInstanceRef.current = chart;

    // Process data for noise reduction
    let processedData = data;

    // Apply data aggregation if enabled
    if (dataAggregationSeconds > 1) {
      processedData = aggregateData(processedData, dataAggregationSeconds);
    }

    // Add moving average series first (so it appears behind the main line)
    if (showMovingAverage && processedData.length > 0) {
      const maSeries = chart.addSeries(LineSeries, {
        color: "#94a3b8",
        lineWidth: 2,
        lineType: LineType.Simple,
        priceLineVisible: false,
        lastValueVisible: true,
        crosshairMarkerVisible: true,
        crosshairMarkerRadius: 3,
      });
      maSeriesRef.current = maSeries;

      const maData = calculateMovingAverageData(processedData, movingAveragePeriod);
      maSeries.setData(maData);
    }

    // Add main series - choose between line and area based on props
    if (useAreaChart) {
      const areaSeries = chart.addSeries(AreaSeries, {
        lineColor: "#10b981",
        topColor: "rgba(16, 185, 129, 0.4)",
        bottomColor: "rgba(16, 185, 129, 0.05)",
        lineWidth: 2,
        lineType: LineType.Curved,
        lineStyle: lineStyle === 'dotted' ? LineStyle.Dotted :
                  lineStyle === 'dashed' ? LineStyle.Dashed :
                  LineStyle.Solid,
        crosshairMarkerRadius: 4,
        ...seriesOptions,
      });
      seriesRef.current = areaSeries;
      areaSeries.setData(processedData);
    } else {
      const lineSeries = chart.addSeries(LineSeries, {
        color: "#10b981",
        lineWidth: 2,
        lineType: LineType.Curved,
        lineStyle: lineStyle === 'dotted' ? LineStyle.Dotted :
                  lineStyle === 'dashed' ? LineStyle.Dashed :
                  LineStyle.Solid,
        crosshairMarkerRadius: 4,
        // Enhanced line options for smoother appearance
        priceLineVisible: false,
        lastValueVisible: true,
        ...seriesOptions,
      });
      seriesRef.current = lineSeries;
      lineSeries.setData(processedData);
    }

    // Add price line if provided
    if (priceLineOptions && seriesRef.current) {
      seriesRef.current.createPriceLine(priceLineOptions);
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
      maSeriesRef.current = null;
    };
  }, [showMovingAverage, movingAveragePeriod, useAreaChart, dataAggregationSeconds, lineStyle]);

  // Update data when it changes
  useEffect(() => {
    if (seriesRef.current && data.length > 0) {
      // Process data for noise reduction
      let processedData = data;

      // Apply data aggregation if enabled
      if (dataAggregationSeconds > 1) {
        processedData = aggregateData(processedData, dataAggregationSeconds);
      }

      seriesRef.current.setData(processedData);

      // Update moving average data if enabled
      if (showMovingAverage && maSeriesRef.current) {
        const maData = calculateMovingAverageData(processedData, movingAveragePeriod);
        maSeriesRef.current.setData(maData);
      }

      // Auto-scroll to show latest data
      if (chartInstanceRef.current) {
        chartInstanceRef.current.timeScale().scrollToRealTime();
      }
    }
  }, [data, showMovingAverage, movingAveragePeriod, useAreaChart, dataAggregationSeconds]);

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
