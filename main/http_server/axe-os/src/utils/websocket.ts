/**
 * WebSocket connection management
 * Handles connecting, disconnecting, and message processing for WebSockets
 */

export function createWebSocketConnection(
  url: string,
  onMessage: (message: string) => void,
  onOpen?: () => void,
  onClose?: () => void,
  onError?: (error: Event) => void,
  options?: {
    directConnect?: boolean;
    deviceIp?: string;
  }
) {
  // Create WebSocket with the correct URL (relative or absolute)
  let wsUrl = url;

  if (!url.startsWith("ws")) {
    // Development environment with direct connection to device
    if (options?.directConnect && options?.deviceIp) {
      // Make sure the device IP doesn't have protocol prefixes
      const cleanIp = options.deviceIp.replace(/^https?:\/\/|^ws:\/\//, "");
      // Ensure trailing slash is handled correctly
      const cleanPath = url.startsWith("/") ? url : `/${url}`;
      wsUrl = `ws://${cleanIp}${cleanPath}`;
    } else {
      // Standard connection through same host
      wsUrl = `${window.location.protocol === "https:" ? "wss:" : "ws:"}//${
        window.location.host
      }${url}`;
    }
  }

  console.log(`Connecting to WebSocket: ${wsUrl}`);

  // Add connection timeout handling
  let connectionTimeout: number | null = null;

  const socket = new WebSocket(wsUrl);

  socket.onopen = () => {
    console.log("WebSocket connection established");
    if (connectionTimeout) {
      clearTimeout(connectionTimeout);
      connectionTimeout = null;
    }
    onOpen?.();
  };

  socket.onmessage = (event) => {
    try {
      onMessage(event.data);
    } catch (error) {
      console.error("Error processing WebSocket message:", error);
    }
  };

  socket.onclose = (event) => {
    if (connectionTimeout) {
      clearTimeout(connectionTimeout);
      connectionTimeout = null;
    }

    console.log(
      `WebSocket connection closed: Code: ${event.code}, Reason: ${
        event.reason || "No reason provided"
      }`
    );
    onClose?.();
  };

  socket.onerror = (error) => {
    console.error("WebSocket error:", error);
    onError?.(error);
  };

  // Set connection timeout (10 seconds)
  connectionTimeout = window.setTimeout(() => {
    if (socket.readyState !== WebSocket.OPEN) {
      console.warn("WebSocket connection timeout");
      socket.close();
      onError?.(new Event("timeout"));
    }
    connectionTimeout = null;
  }, 10000);

  // Return functions to send messages and close the connection
  return {
    send: (message: string) => {
      if (socket.readyState === WebSocket.OPEN) {
        socket.send(message);
      } else {
        console.warn("WebSocket is not open. Cannot send message.");
      }
    },
    close: () => {
      if (connectionTimeout) {
        clearTimeout(connectionTimeout);
        connectionTimeout = null;
      }
      socket.close();
    },
    isOpen: () => socket.readyState === WebSocket.OPEN,
  };
}
