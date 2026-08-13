const target = process.env.BITAXE_IP || 'http://192.168.1.100';
const targetUrl = target.startsWith('http') ? target : `http://${target}`;

module.exports = {
  "/api": {
    "target": targetUrl,
    "secure": false,
    "ws": true,
    "changeOrigin": true,
    "logLevel": "debug",
    "onProxyReq": (proxyReq, req, res) => {
      // Rewrite the Origin header to match the target device
      // to bypass the device's private network CORS validation.
      proxyReq.setHeader('origin', targetUrl);
    },
    "onProxyReqWs": (proxyReq, req, socket, options, head) => {
      // Rewrite the Origin header for WebSocket upgrade requests as well.
      proxyReq.setHeader('origin', targetUrl);
    }
  }
};
