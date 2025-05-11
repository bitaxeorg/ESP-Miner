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
  const ESP32_IP = env.VITE_ESP32_IP || "localhost";

  return {
    plugins: [preact(), tailwindcss()],
    build: {
      outDir: resolve(__dirname, "dist/axe-os"),
    },
    server: {
      proxy: {
        // Proxy API requests to the ESP32 device
        "/api": {
          target: `http://${ESP32_IP}`,
          changeOrigin: true,
          secure: false,
          // Enable debug logging for proxy requests
          configure: (proxy, _options) => {
            proxy.on("error", (err, _req, _res) => {
              console.log("proxy error", err);
            });
            proxy.on("proxyReq", (proxyReq, req, _res) => {
              console.log("Sending Request to Target:", req.method, req.url);
              console.log("Request Headers:", proxyReq.getHeaders());
            });
            proxy.on("proxyRes", (proxyRes, req, _res) => {
              console.log("Received Response from Target:", proxyRes.statusCode, req.url);
              console.log("Response Headers:", proxyRes.headers);
            });
          },
        },
      },
    },
  };
});
