# ESP32 Preact API Client

This project is a Preact-based web client for interacting with ESP32 API endpoints. It demonstrates how to fetch data from an ESP32 device and display it in a web interface.

## CORS and API Access Configuration

This project uses a development proxy to avoid CORS issues when accessing the ESP32 API. The proxy is configured in `vite.config.ts` and forwards requests from `/api/*` to the ESP32 device at `http://10.1.1.50/api/*`.

### Options for handling CORS

1. **Development Proxy (Current Solution)**

   - The Vite development server proxies API requests to the ESP32
   - No changes needed on the ESP32 device
   - Only works during development

2. **Configure ESP32 CORS Headers (Recommended for Production)**

   - Modify the ESP32 HTTP server code to include CORS headers:

   ```c
   httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
   httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
   httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Accept");
   ```

   - This approach works for both development and production
   - See HTTP server example code: https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server/simple

3. **Third-party CORS Proxy**
   - Use a service like https://cors-anywhere.herokuapp.com/
   - Example: `fetch('https://cors-anywhere.herokuapp.com/http://10.1.1.50/api/system/info')`
   - Suitable for demonstrations but not recommended for production

## Getting Started

### Prerequisites

- Node.js (v14 or later)
- npm or yarn
- ESP32 device with API server running on your network

### Installation

1. Clone this repository
2. Install dependencies

   ```bash
   npm install
   ```

3. Update the ESP32 IP address in `vite.config.ts` if needed

   ```typescript
   proxy: {
     '/api': {
       target: 'http://YOUR_ESP32_IP',
       changeOrigin: true,
       secure: false
     }
   }
   ```

4. Start the development server

   ```bash
   npm run dev
   ```

5. Open your browser to the URL shown in the terminal (typically http://localhost:5173)

## Building for Production

To build for production:

```bash
npm run build
```

The output files will be in the `dist` directory.

**Note:** For production deployment, you should configure CORS headers on your ESP32 server or set up a production-ready proxy server.

## License

[MIT](https://choosealicense.com/licenses/mit/)
