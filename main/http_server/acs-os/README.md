# Axe-OS Web Interface

A lightweight web interface for ESP32-based Bitcoin mining devices (Bitaxe/ESP-Miner). Built with Preact and optimized for embedded systems with strict memory and storage constraints.

## 🎯 Overview

This project replaces the original Angular-based frontend with a minimal Preact implementation designed specifically for embedded ESP32 devices. The entire application is served directly from the device's internal HTTP server and must fit within a 4MB flash memory constraint alongside the device firmware.

## ✨ Features

- **Device Monitoring**: Real-time mining statistics and performance metrics
- **Configuration Management**: Wi-Fi settings, mining pool configuration, and device settings
- **Analytics Dashboard**: Mining performance snapshot scorecards
- **System Management**: Firmware updates, logs viewing, and advanced settings
- **TBD: Theme Support**: Customizable UI themes
- **Offline-First**: Runs entirely from the device with no external dependencies

## 🛠 Tech Stack

- **[Preact](https://preactjs.com/)** - Lightweight React alternative (3KB)
- **[Vite](https://vitejs.dev/)** - Fast build tool and dev server
- **[Tailwind CSS](https://tailwindcss.com/)** - Utility-first CSS framework
- **[Lightweight Charts](https://tradingview.github.io/lightweight-charts/)** - Performance charts
- **[Lucide Preact](https://lucide.dev/)** - Icon library
- **TypeScript** - Type safety and better DX

## 🚀 Quick Start

### Prerequisites

- Node.js 16+ and npm
- ESP32 device running compatible firmware
- Device accessible on your local network

### Development Setup

1. **Clone and install dependencies**

   ```bash
   git clone <repository-url>
   cd main/http_server/acs-os

   npm install
   ```

2. **Configure API endpoint**

   Make a copy of `.env.example` and rename it `.env.developmoent`:

   ```bash
   # Replace with your device's IP address
   VITE_API_URL=http://192.168.1.100
   ```

3. **Start development server**

   ```bash
   npm run dev
   ```

4. **Open in browser**

   Navigate to `http://localhost:5173`

### Production Build

```bash
# Build optimized bundle with compression
npm run build

# Preview production build locally
npm run preview
```

The build outputs to `dist/acs-os/` with pre-compressed `.gz` and `.br` files for optimal embedded serving.

## Known Bugs

1. Headers too long API response => Clear your cookies and try again.
2. Patch Requests failing => Need to troubleshoot this.

## 📁 Project Structure

```
src/
├── components/          # Reusable UI components
├── pages/              # Route-based page components
│   ├── Home/           # Dashboard and overview
│   ├── Analytics/      # Performance charts
│   ├── Settings/       # Device configuration
│   ├── Wifi/           # Network settings
│   ├── Pool/           # Mining pool config
│   ├── Logs/           # System logs
│   └── Updates/        # Firmware management
├── context/            # Global state management
├── utils/              # Helper functions and API client
├── assets/             # Static assets
└── style.css           # Global styles
```

## 🔧 Configuration

### Environment Variables

| Variable       | Description               | Default             |
| -------------- | ------------------------- | ------------------- |
| `VITE_API_URL` | ESP32 device API endpoint | `http://10.1.1.168` |

### Build Configuration

The application is optimized for embedded deployment:

- **Bundle size**: Optimized for <500KB compressed
- **Asset handling**: No inlined assets for predictable sizing
- **Compression**: Pre-compressed with gzip and brotli
- **Hash-based routing**: No server-side routing required

## 🎨 Customization

### Themes (WIP)

The application supports custom themes through Tailwind CSS variables. Modify theme configurations in the Theme settings page or customize the CSS variables directly.

### Adding Features

1. Create new page components in `src/pages/`
2. Add routing logic in the main application router
3. Implement API calls in `src/utils/`
4. Follow the existing component patterns for consistency

## 🔍 API Integration

The application communicates with the ESP32 device through REST API endpoints:

## 🐛 Troubleshooting

### Common Issues

**Build size too large**

- Check bundle analyzer output
- Remove unused dependencies
- Optimize asset sizes

**API connection issues**

- Verify device IP address in configuration
- Check API response, if Headers are too long delete your cookies on localhost
- Check network connectivity
- Ensure CORS headers are configured on device

## 📊 Performance

- **Bundle size**: ~400KB compressed
- **Cold start**: <2s on ESP32
- **Memory usage**: <512KB runtime
- **Load time**: <3s over Wi-Fi

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Follow the existing code style and patterns
4. Test on actual hardware when possible
5. Submit a pull request

---

Built with ❤️ for the Bitcoin mining community
