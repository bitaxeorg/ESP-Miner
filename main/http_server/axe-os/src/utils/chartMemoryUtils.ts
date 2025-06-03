/**
 * Chart Memory and Configuration Utilities
 *
 * This file provides utilities for calculating memory usage and
 * configuration constants for different chart time periods.
 */

export interface ChartMemoryInfo {
  dataPoints: number;
  estimatedMemoryKB: number;
  timeSpanHours: number;
  updateIntervalSeconds: number;
}

export interface ChartTimeConfig {
  name: string;
  dataPoints: number;
  hours: number;
  intervalSeconds: number;
  description: string;
}

// Pre-defined time configurations
export const CHART_CONFIGS: Record<string, ChartTimeConfig> = {
  SHORT: {
    name: "Short",
    dataPoints: 360,      // 30 minutes
    hours: 0.5,
    intervalSeconds: 5,
    description: "30 minutes of data"
  },
  MEDIUM: {
    name: "Medium",
    dataPoints: 720,      // 1 hour
    hours: 1,
    intervalSeconds: 5,
    description: "1 hour of data"
  },
  LONG: {
    name: "Long",
    dataPoints: 1440,     // 2 hours
    hours: 2,
    intervalSeconds: 5,
    description: "2 hours of data"
  },
  EXTENDED: {
    name: "Extended",
    dataPoints: 2880,     // 4 hours
    hours: 4,
    intervalSeconds: 5,
    description: "4 hours of data"
  },
  FULL_DAY: {
    name: "Full Day",
    dataPoints: 4320,     // 6 hours (practical limit)
    hours: 6,
    intervalSeconds: 5,
    description: "6 hours of data"
  }
};

/**
 * Calculate memory usage for chart data
 */
export function calculateChartMemory(dataPoints: number): ChartMemoryInfo {
  // Each data point consists of:
  // - time: 8 bytes (64-bit timestamp)
  // - value: 8 bytes (64-bit float)
  // - JavaScript object overhead: ~16 bytes
  // Total per point: ~32 bytes (conservative estimate)

  const bytesPerPoint = 32;
  const totalBytes = dataPoints * bytesPerPoint;
  const totalKB = totalBytes / 1024;

  return {
    dataPoints,
    estimatedMemoryKB: Math.round(totalKB * 100) / 100,
    timeSpanHours: dataPoints * 5 / 3600, // Assuming 5-second intervals
    updateIntervalSeconds: 5
  };
}

/**
 * Get recommended configuration based on available memory or requirements
 */
export function getRecommendedConfig(maxMemoryKB?: number): ChartTimeConfig {
  const configs = Object.values(CHART_CONFIGS);

  if (!maxMemoryKB) {
    // Default recommendation: 2 hours
    return CHART_CONFIGS.LONG;
  }

  // Find the largest config that fits within memory limit
  for (let i = configs.length - 1; i >= 0; i--) {
    const config = configs[i];
    const memoryInfo = calculateChartMemory(config.dataPoints);

    if (memoryInfo.estimatedMemoryKB <= maxMemoryKB) {
      return config;
    }
  }

  // If nothing fits, return the smallest config
  return CHART_CONFIGS.SHORT;
}

/**
 * Calculate optimal data points for a given time span and interval
 */
export function calculateDataPoints(hours: number, intervalSeconds: number = 5): number {
  return Math.floor((hours * 3600) / intervalSeconds);
}

/**
 * Format memory size for display
 */
export function formatMemorySize(kb: number): string {
  if (kb < 1) {
    return `${Math.round(kb * 1024)} bytes`;
  } else if (kb < 1024) {
    return `${Math.round(kb * 100) / 100} KB`;
  } else {
    return `${Math.round(kb / 1024 * 100) / 100} MB`;
  }
}

/**
 * Get performance recommendations based on data points
 */
export function getPerformanceRecommendations(dataPoints: number): string[] {
  const recommendations: string[] = [];

  if (dataPoints > 5000) {
    recommendations.push("Consider using data aggregation to reduce noise");
    recommendations.push("Enable smoothing for better visual experience");
    recommendations.push("Use area charts sparingly for better performance");
  }

  if (dataPoints > 10000) {
    recommendations.push("Consider reducing update frequency");
    recommendations.push("Implement data downsampling for older data points");
  }

  if (dataPoints < 100) {
    recommendations.push("Consider increasing data retention for better trends");
  }

  return recommendations;
}

/**
 * Validate if a configuration is feasible
 */
export function validateChartConfig(config: ChartTimeConfig): {
  isValid: boolean;
  warnings: string[];
  memoryInfo: ChartMemoryInfo;
} {
  const warnings: string[] = [];
  const memoryInfo = calculateChartMemory(config.dataPoints);

  // Memory warnings
  if (memoryInfo.estimatedMemoryKB > 500) {
    warnings.push(`High memory usage: ${formatMemorySize(memoryInfo.estimatedMemoryKB)}`);
  }

  if (memoryInfo.estimatedMemoryKB > 1000) {
    warnings.push("Consider reducing data points for better performance");
  }

  // Data point warnings
  if (config.dataPoints > 10000) {
    warnings.push("Very large dataset may impact chart rendering performance");
  }

  if (config.dataPoints < 10) {
    warnings.push("Too few data points may not provide meaningful insights");
  }

  // Interval warnings
  if (config.intervalSeconds < 1) {
    warnings.push("Update interval too frequent may impact system performance");
  }

  if (config.intervalSeconds > 60) {
    warnings.push("Update interval too slow may miss important changes");
  }

  const isValid = warnings.length === 0 || warnings.every(w => !w.includes("may impact"));

  return {
    isValid,
    warnings,
    memoryInfo
  };
}