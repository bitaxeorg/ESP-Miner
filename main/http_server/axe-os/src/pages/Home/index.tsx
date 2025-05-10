import preactLogo from "../../assets/preact.svg";
import { useState, useEffect, useRef } from "preact/hooks";
import "./style.css";

export function Home() {
  const [systemInfo, setSystemInfo] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [fetchStatus, setFetchStatus] = useState("");
  const [useDirectUrl, setUseDirectUrl] = useState(false);
  const [pingStatus, setPingStatus] = useState(null);
  const [updateStatus, setUpdateStatus] = useState(null);
  const [updateResponse, setUpdateResponse] = useState(null);
  const [showUpdateResponse, setShowUpdateResponse] = useState(false);
  const [authToken, setAuthToken] = useState("");
  const [inspectorUrl, setInspectorUrl] = useState(`http://10.1.1.50/api/system/info`);
  const [inspectorResults, setInspectorResults] = useState(null);
  const inspectorIframeRef = useRef(null);

  const ESP32_IP = "10.1.1.50";

  // Function to analyze Angular app requests
  const analyzeAngularRequests = async () => {
    setInspectorResults({
      status: "pending",
      message: "Analyzing requests from Angular app...",
      details: [],
    });

    try {
      // First try to load a page from the ESP32 to check for basic connectivity
      const testResponse = await fetch(`http://${ESP32_IP}`, {
        method: "GET",
        mode: "no-cors", // Just to check if we can reach it
      });

      // Log headers from our requests to see what's currently being sent
      console.log("Retrieving information from Angular app...");

      // Create a new window or tab loading the Angular app
      // This is the most reliable way to inspect what the Angular app is doing
      window.open(`http://${ESP32_IP}`, "_blank", "width=800,height=600");

      setInspectorResults({
        status: "success",
        message: `
          Please follow these steps to check the Angular app's authentication:

          1. In the new browser tab with the Angular app:
             - Open browser developer tools (F12 or right-click and select "Inspect")
             - Go to the Network tab
             - Make sure "Preserve log" is checked
             - Reload the page if needed

          2. Look for API requests to /api/system:
             - Look at the request headers for: Authorization, X-Auth-Token, etc.
             - Check if there are cookies being sent
             - Examine the request payload for any tokens

          3. If you find authentication info, enter it below.
        `,
        details: [
          {
            title: "Check for HTTP Basic Auth",
            description: "Look for 'Authorization: Basic xxx=' headers",
          },
          {
            title: "Check for Bearer Tokens",
            description: "Look for 'Authorization: Bearer xxx' headers",
          },
          {
            title: "Check for Custom Headers",
            description: "Look for X-Auth-Token, X-API-Key, etc.",
          },
          {
            title: "Check localStorage in the Angular app",
            description: "In DevTools Console, type: Object.keys(localStorage)",
          },
        ],
      });
    } catch (err) {
      setInspectorResults({
        status: "error",
        message: `Error connecting to Angular app: ${err.message}`,
        details: [],
      });
    }
  };

  // Function to fetch data from the API
  const fetchSystemInfo = async () => {
    setLoading(true);
    setError(null);
    setFetchStatus("Attempting to fetch data...");

    try {
      // We'll try two approaches: proxy and direct with different options
      const apiUrl = useDirectUrl ? `http://${ESP32_IP}/api/system/info` : "/api/system/info";

      setFetchStatus(`Fetching from ${apiUrl}${useDirectUrl ? " (direct)" : " (via proxy)"}...`);

      const headers = {
        Accept: "application/json",
        "Cache-Control": "no-cache",
        Pragma: "no-cache",
      };

      // Add authorization if provided
      if (authToken) {
        // Try to detect the auth format and add appropriate header
        if (authToken.startsWith("Basic ") || authToken.startsWith("Bearer ")) {
          headers["Authorization"] = authToken;
        } else if (authToken.includes(":")) {
          // Looks like username:password format, encode as Basic auth
          const encoded = btoa(authToken);
          headers["Authorization"] = `Basic ${encoded}`;
        } else {
          // Assume it's a token and use Bearer format by default
          headers["Authorization"] = `Bearer ${authToken}`;
          // Also try as a custom token header in case that's what's needed
          headers["X-Auth-Token"] = authToken;
        }
      }

      const commonOptions = {
        method: "GET",
        headers,
      };

      // Add connection-specific options based on method
      const fetchOptions = useDirectUrl
        ? { ...commonOptions, mode: "cors" as RequestMode }
        : { ...commonOptions, credentials: "same-origin" as RequestCredentials };

      console.log("Fetch options:", fetchOptions);
      const response = await fetch(apiUrl, fetchOptions);

      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }

      const data = await response.json();
      setSystemInfo(data);
      setFetchStatus("Data fetched successfully!");
    } catch (err) {
      setError(err.message || "Failed to fetch system info");
      setFetchStatus(`Error: ${err.message}`);
      console.error("Fetch error:", err);
    } finally {
      setLoading(false);
    }
  };

  // Function to update system settings
  const updateSystemSettings = async () => {
    setUpdateStatus("Sending update...");
    setUpdateResponse(null);
    setShowUpdateResponse(false);

    try {
      // Payload for system update
      const payload = {
        stratumURL: "solo.ckpool.org",
      };

      // Determine API URL based on connection method
      const apiUrl = useDirectUrl ? `http://${ESP32_IP}/api/system` : "/api/system";

      console.log("Updating system settings at:", apiUrl);
      console.log("Payload:", payload);

      const headers = {
        "Content-Type": "application/json",
        Accept: "application/json",
      };

      // Add authorization if provided
      if (authToken) {
        // Try to detect the auth format and add appropriate header
        if (authToken.startsWith("Basic ") || authToken.startsWith("Bearer ")) {
          headers["Authorization"] = authToken;
        } else if (authToken.includes(":")) {
          // Looks like username:password format, encode as Basic auth
          const encoded = btoa(authToken);
          headers["Authorization"] = `Basic ${encoded}`;
        } else {
          // Assume it's a token and use Bearer format by default
          headers["Authorization"] = `Bearer ${authToken}`;
          // Also try as a custom token header in case that's what's needed
          headers["X-Auth-Token"] = authToken;
        }
      }

      // Set up request options
      const updateOptions = {
        method: "PATCH",
        headers,
        body: JSON.stringify(payload),
        ...(useDirectUrl
          ? { mode: "cors" as RequestMode }
          : { credentials: "same-origin" as RequestCredentials }),
      };

      const response = await fetch(apiUrl, updateOptions);

      // Parse response as JSON even if it's an error
      const responseData = await response.json();
      setUpdateResponse(responseData);
      setShowUpdateResponse(true);

      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }

      setUpdateStatus("Update successful!");

      // Refresh system info after successful update
      fetchSystemInfo();
    } catch (err) {
      setUpdateStatus(`Update failed: ${err.message}`);
      console.error("Update error:", err);
    }
  };

  // Function to ping ESP32 to check connectivity
  const pingESP32 = async () => {
    setPingStatus("Testing connection...");

    try {
      // Create an image element to test if the device is reachable at all
      // This bypasses some CORS restrictions since images can load cross-origin
      const timestamp = new Date().getTime();
      const img = new Image();

      // Set up timeout to handle unreachable host
      const timeout = setTimeout(() => {
        setPingStatus("Failed: Connection timed out");
      }, 5000);

      // Set up handlers for the image load attempt
      img.onload = () => {
        clearTimeout(timeout);
        setPingStatus("Success: ESP32 is reachable!");
      };

      img.onerror = () => {
        clearTimeout(timeout);
        setPingStatus("Failed: ESP32 is not reachable or blocked requests");
      };

      // Attempt to load the favicon or any small image from the ESP32
      // Adding a timestamp prevents caching
      img.src = `http://${ESP32_IP}/favicon.ico?t=${timestamp}`;
    } catch (err) {
      setPingStatus(`Error: ${err.message}`);
    }
  };

  // Toggle between direct URL and proxy
  const toggleFetchMethod = () => {
    setUseDirectUrl(!useDirectUrl);
  };

  // Fetch data on component mount
  useEffect(() => {
    fetchSystemInfo();
    pingESP32();
  }, [useDirectUrl]); // Refetch when URL method changes

  return (
    <div class='home'>
      <a href='https://preactjs.com' target='_blank'>
        <img src={preactLogo} alt='Preact logo' height='160' width='160' />
      </a>
      <h1>ESP32 API Client</h1>

      <div class='connection-status'>
        <div class='connection-toggle'>
          <label>
            <input type='checkbox' checked={useDirectUrl} onChange={toggleFetchMethod} />
            Use direct URL (bypass proxy)
          </label>
        </div>

        <div
          class={`ping-status ${
            pingStatus && pingStatus.includes("Success") ? "success" : pingStatus ? "error" : ""
          }`}
        >
          <strong>ESP32 Connectivity:</strong> {pingStatus || "Not tested"}
          <button
            class='small-button'
            onClick={pingESP32}
            disabled={!pingStatus || pingStatus.includes("Testing")}
          >
            Test Again
          </button>
        </div>
      </div>

      <div class='auth-section'>
        <h2>Authentication</h2>
        <p>You received a 401 Unauthorized error. Add authentication credentials below:</p>

        <div class='auth-input'>
          <input
            type='text'
            value={authToken}
            placeholder='Auth token, API key, or username:password'
            onChange={(e) => setAuthToken((e.target as HTMLInputElement).value)}
          />
          <button class='small-button' onClick={analyzeAngularRequests}>
            Analyze Angular App
          </button>
        </div>

        <div class='auth-help'>
          <p>
            <strong>Formats accepted:</strong>
          </p>
          <ul>
            <li>
              <code>username:password</code> - Will be encoded as Basic auth
            </li>
            <li>
              <code>Basic dXNlcm5hbWU6cGFzc3dvcmQ=</code> - Basic auth (already encoded)
            </li>
            <li>
              <code>Bearer eyJhbG...</code> - JWT or other bearer token
            </li>
            <li>
              <code>eyJhbG...</code> - Raw token (will try as Bearer auth and X-Auth-Token)
            </li>
          </ul>
        </div>

        {inspectorResults && (
          <div class={`inspector-results ${inspectorResults.status}`}>
            <h3>Authentication Inspector Results</h3>
            <div class='inspector-message'>{inspectorResults.message}</div>

            {inspectorResults.details.length > 0 && (
              <div class='inspector-details'>
                <h4>Check for these authentication methods:</h4>
                <ul>
                  {inspectorResults.details.map((detail, index) => (
                    <li key={index}>
                      <strong>{detail.title}:</strong> {detail.description}
                    </li>
                  ))}
                </ul>
              </div>
            )}
          </div>
        )}
      </div>

      <div class='fetch-status'>Status: {fetchStatus}</div>

      <div class='system-info'>
        <h2>System Information</h2>
        {loading && <p>Loading...</p>}
        {error && (
          <div class='error'>
            <p>Error: {error}</p>
            {error.includes("401") && (
              <p>
                The server requires authentication. Please check the Authentication section above.
              </p>
            )}
          </div>
        )}
        {systemInfo && <pre class='json-output'>{JSON.stringify(systemInfo, null, 2)}</pre>}
      </div>

      <div class='system-update'>
        <h2>Update System Settings</h2>
        <p>Send a PATCH request to update the system settings with mining pool information:</p>

        <div class='code-preview'>
          <pre>
            {`{
  "stratumURL": "solo.ckpool.org"
}`}
          </pre>
        </div>

        <button
          class='update-button'
          onClick={updateSystemSettings}
          disabled={loading || updateStatus === "Sending update..."}
        >
          {updateStatus === "Sending update..." ? "Sending..." : "Update System Settings"}
        </button>

        {updateStatus && (
          <div
            class={`update-status ${
              updateStatus.includes("successful")
                ? "success"
                : updateStatus.includes("failed")
                ? "error"
                : "pending"
            }`}
          >
            {updateStatus}
          </div>
        )}

        {showUpdateResponse && updateResponse && (
          <div class='response-section'>
            <h3>Response from Server:</h3>
            <pre class='json-output'>{JSON.stringify(updateResponse, null, 2)}</pre>
          </div>
        )}
      </div>

      <p class='note'>
        <strong>Current Method:</strong> {useDirectUrl ? "Direct connection" : "Vite proxy"}
        <br />
        <strong>URL:</strong>{" "}
        {useDirectUrl ? `http://${ESP32_IP}/api/system/info` : "/api/system/info"}
        <br />
        <br />
        <strong>Troubleshooting Tips:</strong>
        <ul>
          <li>Make sure your ESP32 is powered on and connected to your network</li>
          <li>Verify that your computer and the ESP32 are on the same network</li>
          <li>Check that port 80 (HTTP) is not blocked by any firewalls</li>
          <li>The ESP32 might be restricting connections to specific IPs</li>
          <li>Try using a different browser to test for browser-specific issues</li>
          <li>
            <strong>Check if the ESP32 uses a default admin password</strong> - Many devices have
            default credentials
          </li>
          <li>
            <strong>IP Whitelist</strong> - The ESP32 might be configured to only accept requests
            from localhost
          </li>
        </ul>
      </p>

      <div class='button-row'>
        <button onClick={fetchSystemInfo} disabled={loading}>
          {loading ? "Fetching..." : "Refresh Data"}
        </button>
      </div>
    </div>
  );
}

function Resource(props) {
  return (
    <a href={props.href} target='_blank' class='resource'>
      <h2>{props.title}</h2>
      <p>{props.description}</p>
    </a>
  );
}
