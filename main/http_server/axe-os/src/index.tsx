import { render } from "preact";
import { LocationProvider, Router, Route } from "preact-iso";
import { Layout } from "./components/Layout";
import { Home } from "./pages/Home/index.jsx";
import { Settings } from "./pages/Settings/index.jsx";
import { Theme } from "./pages/Theme/index";
import { Miners } from "./pages/Miners/index";
import { NotFound } from "./pages/_404.jsx";
import { ThemeProvider } from "./context/ThemeContext";
import "./style.css";

export function App() {
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
      </LocationProvider>
    </ThemeProvider>
  );
}

render(<App />, document.getElementById("app"));
