import { render } from "preact";
import { LocationProvider, Router, Route } from "preact-iso";
import { Layout } from "./components/Layout";
import { HomePage } from "./pages/Home/index.jsx";
import { Settings } from "./pages/Settings/index.js";
import { Theme } from "./pages/Theme/index";
import { Miners } from "./pages/Miners/index";
import { NotFound } from "./pages/_404.jsx";
import { ThemeProvider } from "./context/ThemeContext";
import { ToastProvider, useToast } from "./context/ToastContext";
import { OverheatWarningProvider, useOverheatWarning } from "./context/OverheatWarningContext";
import { ToastContainer } from "./components/Toast";
import { setOverheatWarningCheck } from "./utils/api";
import "./style.css";
import { LogsPage } from "./pages/Logs";
import { PoolsPage } from "./pages/Pool";
import { WifiPage } from "./pages/Wifi";
import ChartPage from "./pages/Chart";
import UpdatesPage from "./pages/Updates";
import { useEffect } from "preact/hooks";

function AppContent() {
  const { toasts, hideToast } = useToast();
  const { checkOverheatStatus } = useOverheatWarning();

  useEffect(() => {
    // Set up the global overheat warning check
    setOverheatWarningCheck(checkOverheatStatus);
  }, [checkOverheatStatus]);

  return (
    <ThemeProvider>
      <LocationProvider>
        <Layout>
          <Router>
            <Route path='/' component={HomePage} />
            <Route path='/logs' component={LogsPage} />
            <Route path='/settings' component={Settings} />
            <Route path='/theme' component={Theme} />
            <Route path='/miners' component={Miners} />
            <Route path='/pools' component={PoolsPage} />
            <Route path='/wifi' component={WifiPage} />
            <Route path='/chart' component={ChartPage} />
            <Route path='/updates' component={UpdatesPage} />
            <Route default component={NotFound} />
          </Router>
        </Layout>
        <ToastContainer toasts={toasts} onClose={hideToast} />
      </LocationProvider>
    </ThemeProvider>
  );
}

export function App() {
  return (
    <ToastProvider>
      <OverheatWarningProvider>
        <AppContent />
      </OverheatWarningProvider>
    </ToastProvider>
  );
}

render(<App />, document.getElementById("app"));
