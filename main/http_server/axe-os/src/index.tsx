import { render } from "preact";
import { LocationProvider, Router, Route } from "preact-iso";
import { Layout } from "./components/Layout";
import { HomePage } from "./pages/Home/index.jsx";
import { Settings } from "./pages/Settings/index.jsx";
import { Theme } from "./pages/Theme/index";
import { Miners } from "./pages/Miners/index";
import { NotFound } from "./pages/_404.jsx";
import { ThemeProvider } from "./context/ThemeContext";
import { ToastProvider, useToast } from "./context/ToastContext";
import { ToastContainer } from "./components/Toast";
import "./style.css";
import { LogsPage } from "./pages/Logs";
import { PoolsPage } from "./pages/Pool";
function AppContent() {
  const { toasts, hideToast } = useToast();

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
      <AppContent />
    </ToastProvider>
  );
}

render(<App />, document.getElementById("app"));
