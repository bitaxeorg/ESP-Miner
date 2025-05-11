import { render } from "preact";
import { LocationProvider, Router, Route } from "preact-iso";
import { Layout } from "./components/Layout";
import { Home } from "./pages/Home/index.jsx";
import { Settings } from "./pages/Settings/index.jsx";
import { Theme } from "./pages/Theme/index";
import { NotFound } from "./pages/_404.jsx";
import "./style.css";

export function App() {
  return (
    <LocationProvider>
      <Layout>
        <Router>
          <Route path='/' component={Home} />
          <Route path='/settings' component={Settings} />
          <Route path='/theme' component={Theme} />
          <Route default component={NotFound} />
        </Router>
      </Layout>
    </LocationProvider>
  );
}

render(<App />, document.getElementById("app"));
