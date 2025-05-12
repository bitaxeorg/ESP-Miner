import { render } from "preact";
import { LocationProvider, Router, Route } from "preact-iso";
import { Layout } from "./components/Layout";
import { Home } from "./pages/Home/index.jsx";
import { Settings } from "./pages/Settings/index.jsx";
import { Theme } from "./pages/Theme/index";
import { Miners } from "./pages/Miners/index";
import { NotFound } from "./pages/_404.jsx";
import { ThemeProvider } from "./context/ThemeContext";
import { ToastProvider, useToast } from "./context/ToastContext";
import { ToastContainer } from "./components/Toast";
import "./style.css";

function AppContent() {
  const { toasts, hideToast } = useToast();

  return (
    <ThemeProvider>
      <LocationProvider>
        <Layout>
          <Router>
            <Route path='/' component={Home} />
            <Route path='/settings' component={Settings} />
            <Route path='/theme' component={Theme} />
            <Route path='/miners' component={Miners} />
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
