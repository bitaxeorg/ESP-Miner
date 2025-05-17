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
  const API_TARGET = isProd ? undefined : env.VITE_API_URL || "http://10.1.1.168";

  return {
    plugins: [preact(), tailwindcss()],
    build: {
      outDir: resolve(__dirname, "dist/axe-os"),
    },
    server: {
      proxy:
        !isProd && API_TARGET
          ? {
              // Proxy API requests to the target device
              "/api": {
                target: API_TARGET,
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
            }
          : {},
    },
  };
});
