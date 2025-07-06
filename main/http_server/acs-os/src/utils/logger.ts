/**
 * Production-safe logging utilities
 * Logs are disabled in production builds to prevent sensitive data exposure
 */

type LogLevel = 'debug' | 'info' | 'warn' | 'error';

interface LoggerConfig {
  level: LogLevel;
  enableConsole: boolean;
  enablePersist: boolean;
}

class Logger {
  private config: LoggerConfig;
  private levels: Record<LogLevel, number> = {
    debug: 0,
    info: 1,
    warn: 2,
    error: 3
  };

  constructor(config?: Partial<LoggerConfig>) {
    // Default to production-safe settings
    this.config = {
      level: import.meta.env.PROD ? 'warn' : 'debug',
      enableConsole: !import.meta.env.PROD,
      enablePersist: false,
      ...config
    };
  }

  private shouldLog(level: LogLevel): boolean {
    return this.levels[level] >= this.levels[this.config.level];
  }

  private formatMessage(level: LogLevel, message: string, data?: any): string {
    const timestamp = new Date().toISOString();
    const prefix = `[${timestamp}] [${level.toUpperCase()}]`;
    return data ? `${prefix} ${message}` : `${prefix} ${message}`;
  }

  debug(message: string, data?: any) {
    if (!this.shouldLog('debug')) return;

    if (this.config.enableConsole) {
      if (data) {
        console.log(this.formatMessage('debug', message), data);
      } else {
        console.log(this.formatMessage('debug', message));
      }
    }
  }

  info(message: string, data?: any) {
    if (!this.shouldLog('info')) return;

    if (this.config.enableConsole) {
      if (data) {
        console.info(this.formatMessage('info', message), data);
      } else {
        console.info(this.formatMessage('info', message));
      }
    }
  }

  warn(message: string, data?: any) {
    if (!this.shouldLog('warn')) return;

    if (data) {
      console.warn(this.formatMessage('warn', message), data);
    } else {
      console.warn(this.formatMessage('warn', message));
    }
  }

  error(message: string, error?: any) {
    if (!this.shouldLog('error')) return;

    if (error) {
      console.error(this.formatMessage('error', message), error);
    } else {
      console.error(this.formatMessage('error', message));
    }
  }

  // Utility method for API responses in development
  apiResponse(operation: string, response: any) {
    this.debug(`${operation} response:`, response);
  }

  // Utility method for API errors
  apiError(operation: string, error: any) {
    this.error(`Failed to ${operation}:`, error);
  }
}

// Create singleton instance
export const logger = new Logger();

// Export for testing or custom configurations
export { Logger };
