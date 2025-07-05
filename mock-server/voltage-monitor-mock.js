const express = require('express');
const cors = require('cors');
const app = express();

// Configuration
const CONFIG = {
  chains: 2,           // Number of chains
  asicsPerChain: 12,   // ASICs per chain
  port: 3000,          // Server port
  updateInterval: 5000 // Data update interval (ms)
};

// Parse command line arguments
const args = process.argv.slice(2);
args.forEach((arg, index) => {
  if (arg === '--chains' && args[index + 1]) {
    CONFIG.chains = parseInt(args[index + 1]);
  }
  if (arg === '--asics' && args[index + 1]) {
    CONFIG.asicsPerChain = parseInt(args[index + 1]);
  }
  if (arg === '--port' && args[index + 1]) {
    CONFIG.port = parseInt(args[index + 1]);
  }
});

// Enable CORS for development
app.use(cors());
app.use(express.json());

// Store for dynamic data
let systemData = generateSystemInfo();
let voltageData = generateVoltageData();

// Update data periodically to simulate real hardware
setInterval(() => {
  systemData = generateSystemInfo();
  voltageData = generateVoltageData();
}, CONFIG.updateInterval);

// Helper functions
function randomBetween(min, max) {
  return Math.random() * (max - min) + min;
}

function generateAsicStatus() {
  const rand = Math.random();
  if (rand < 0.8) return 'healthy';
  if (rand < 0.95) return 'degraded';
  return 'fault';
}

function generateSystemInfo() {
  const totalAsics = CONFIG.chains * CONFIG.asicsPerChain;
  const baseHashrate = 500; // GH/s per ASIC
  const totalHashrate = totalAsics * baseHashrate * randomBetween(0.9, 1.1);
  
  return {
    power: randomBetween(10, 15),
    voltage: randomBetween(5000, 5300),
    current: randomBetween(2200, 2400),
    temp: randomBetween(55, 65),
    vrTemp: randomBetween(40, 50),
    maxPower: 25,
    nominalVoltage: 5,
    hashRate: totalHashrate / 1000, // Convert to TH/s
    expectedHashrate: (totalAsics * baseHashrate) / 1000,
    bestDiff: Math.floor(randomBetween(100000, 500000)).toString(),
    bestSessionDiff: Math.floor(randomBetween(50000, 100000)).toString(),
    freeHeap: Math.floor(randomBetween(190000, 210000)),
    coreVoltage: 1200,
    coreVoltageActual: randomBetween(1180, 1220),
    hostname: "MockBitaxe",
    macAddr: "AA:BB:CC:DD:EE:FF",
    ssid: "TestNetwork",
    wifiPass: "password",
    wifiStatus: "Connected!",
    wifiRSSI: randomBetween(-60, -30),
    apEnabled: 0,
    sharesAccepted: Math.floor(randomBetween(1000, 5000)),
    sharesRejected: Math.floor(randomBetween(0, 50)),
    sharesRejectedReasons: [],
    uptimeSeconds: Math.floor(randomBetween(3600, 86400)),
    smallCoreCount: 672,
    ASICModel: "BM1366",
    stratumURL: "public-pool.io",
    stratumPort: 21496,
    stratumUser: "bc1qmockaddress.bitaxe-mock",
    frequency: randomBetween(475, 500),
    version: "2.0",
    axeOSVersion: "2.0",
    boardVersion: "204",
    fanspeed: randomBetween(80, 100),
    fanrpm: randomBetween(3000, 4000),
    overheat_mode: false,
    power_fault: null,
    isUsingFallbackStratum: false,
    responseTime: randomBetween(10, 50)
  };
}

function generateVoltageData() {
  const chains = [];
  
  for (let chainId = 0; chainId < CONFIG.chains; chainId++) {
    const asics = [];
    let totalVoltage = 0;
    let minVoltage = 999;
    let maxVoltage = 0;
    let failedAsics = 0;
    
    for (let asicId = 0; asicId < CONFIG.asicsPerChain; asicId++) {
      const status = generateAsicStatus();
      let voltage;
      
      switch (status) {
        case 'healthy':
          voltage = randomBetween(1.15, 1.25);
          break;
        case 'degraded':
          voltage = randomBetween(1.05, 1.15);
          break;
        case 'fault':
          voltage = randomBetween(0.5, 0.9);
          failedAsics++;
          break;
      }
      
      asics.push({
        id: asicId,
        voltage: voltage,
        valid: status !== 'fault'
      });
      
      totalVoltage += voltage;
      minVoltage = Math.min(minVoltage, voltage);
      maxVoltage = Math.max(maxVoltage, voltage);
    }
    
    const avgVoltage = totalVoltage / CONFIG.asicsPerChain;
    const suggestedFreq = Math.round(350 + (avgVoltage - 1.0) * 500); // Frequency based on voltage
    
    chains.push({
      chain_id: chainId,
      total_voltage: totalVoltage,
      average_voltage: avgVoltage,
      min_voltage: minVoltage,
      max_voltage: maxVoltage,
      failed_asics: failedAsics,
      suggested_frequency: suggestedFreq,
      asics: asics
    });
  }
  
  return {
    enabled: true,
    hardware_present: true,
    scan_interval_ms: 1000,
    chains: chains
  };
}

// API Endpoints
app.get('/api/voltage', (req, res) => {
  res.json(voltageData);
});

app.get('/api/system/info', (req, res) => {
  res.json(systemData);
});

app.get('/api/system/statistics/dashboard', (req, res) => {
  // Generate time series data for charts
  const timestamps = [];
  const statistics = [];
  const currentTime = Date.now();
  
  for (let i = 0; i < 10; i++) {
    const timestamp = currentTime - (9 - i) * 5000; // 5 second intervals
    const hashrate = systemData.hashRate * randomBetween(0.95, 1.05);
    const temp = systemData.temp + randomBetween(-2, 2);
    const power = systemData.power + randomBetween(-0.5, 0.5);
    
    statistics.push([hashrate, temp, power, timestamp]);
  }
  
  res.json({
    currentTimestamp: currentTime,
    statistics: statistics
  });
});

// Simulate other endpoints that might be called
app.get('/api/system/asic', (req, res) => {
  res.json({
    ASICModel: "BM1366",
    deviceModel: "Ultra",
    swarmColor: "purple",
    asicCount: CONFIG.chains * CONFIG.asicsPerChain,
    defaultFrequency: 485,
    frequencyOptions: [400, 425, 450, 475, 485, 500, 525, 550, 575],
    defaultVoltage: 1200,
    voltageOptions: [1100, 1150, 1200, 1250, 1300]
  });
});

app.get('/api/swarm/info', (req, res) => {
  res.json([]);
});

// Handle preflight requests
app.options('*', cors());

// Start server
app.listen(CONFIG.port, () => {
  console.log(`
🚀 ESP-Miner Mock Server Running!
================================
Port: ${CONFIG.port}
Chains: ${CONFIG.chains}
ASICs per chain: ${CONFIG.asicsPerChain}
Total ASICs: ${CONFIG.chains * CONFIG.asicsPerChain}

API Endpoints:
- http://localhost:${CONFIG.port}/api/voltage
- http://localhost:${CONFIG.port}/api/system/info
- http://localhost:${CONFIG.port}/api/system/statistics/dashboard

Usage:
node voltage-monitor-mock.js [--chains N] [--asics N] [--port N]

Example:
node voltage-monitor-mock.js --chains 4 --asics 16 --port 3000
  `);
});
