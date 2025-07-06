/// <reference types="node" />
import { defineConfig, loadEnv } from "vite";
import preact from "@preact/preset-vite";
import { resolve, dirname } from "path";
import { fileURLToPath } from "url";
import tailwindcss from "@tailwindcss/vite";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// https://vitejs.dev/config/
export default defineConfig(({ mode }) => {
  // Load env file based on `mode` in the current directory
  const env = loadEnv(mode, process.cwd());
  const isProd = mode === "production";

  // Ensure we have a valid API target URL for development
  let API_TARGET = "";
  if (!isProd) {
    API_TARGET = env.VITE_API_URL || "http://10.1.1.168";
    // Validate the URL format
    try {
      new URL(API_TARGET);
    } catch {
      console.warn(`Invalid API_TARGET URL: ${API_TARGET}, falling back to default`);
      API_TARGET = "http://10.1.1.168";
    }
  }

  const proxyConfig =
    !isProd && API_TARGET
      ? {
          "/api": {
            target: API_TARGET,
            changeOrigin: true,
            secure: false, // Set to false for development with self-signed certs
            // Only enable debug logging in development mode
            configure: (proxy, _options) => {
              proxy.on("error", (err, _req, _res) => {
                console.log("proxy error", err);
              });
              proxy.on("proxyReq", (proxyReq, req, _res) => {
                // Strip headers that can cause 431 errors on ESP32
                const headersToRemove = [
                  "sec-ch-ua",
                  "sec-ch-ua-mobile",
                  "sec-ch-ua-platform",
                  "sec-fetch-site",
                  "sec-fetch-mode",
                  "sec-fetch-dest",
                  "cookie",
                  "referer",
                ];

                headersToRemove.forEach((header) => {
                  proxyReq.removeHeader(header);
                });

                // Simplify User-Agent to reduce header size
                proxyReq.setHeader("user-agent", "BitaxeUI/1.0");

                console.log("Sending Request to Target:", req.method, req.url);
                console.log("Request Headers:", Object.keys(proxyReq.getHeaders()));
              });
              proxy.on("proxyRes", (proxyRes, req, _res) => {
                console.log("Received Response from Target:", proxyRes.statusCode, req.url);
                console.log("Response Headers:", Object.keys(proxyRes.headers));
              });
            },
          },
        }
      : {};

  return {
    plugins: [preact(), tailwindcss()],
    build: {
      outDir: resolve(__dirname, "dist/acs-os"),
      assetsInlineLimit: 0, // Disable inlining assets
      // Production optimizations
      minify: isProd ? 'terser' : false,
      terserOptions: isProd ? {
        compress: {
          // Remove console statements in production (except console.error and console.warn)
          drop_console: ['log', 'debug', 'info'],
          drop_debugger: true,
        },
      } : {},
    },
    publicDir: resolve(__dirname, "public"), // Ensure public directory is explicitly set
    server: {
      proxy: proxyConfig,
    },
  };
});
