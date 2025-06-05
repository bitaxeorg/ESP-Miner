/**
 * Production-safe logging utilities
 * Logs are disabled in production builds to prevent sensitive data exposure
 */

const isDevelopment = import.meta.env.DEV;

export const logger = {
  log: (message: string, ...args: any[]) => {
    if (isDevelopment) {
      console.log(message, ...args);
    }
  },

  error: (message: string, ...args: any[]) => {
    if (isDevelopment) {
      console.error(message, ...args);
    } else {
      // In production, still log errors but without sensitive details
      console.error(message);
    }
  },

  warn: (message: string, ...args: any[]) => {
    if (isDevelopment) {
      console.warn(message, ...args);
    }
  },

  info: (message: string, ...args: any[]) => {
    if (isDevelopment) {
      console.info(message, ...args);
    }
  },
};
