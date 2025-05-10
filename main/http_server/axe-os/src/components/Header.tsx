import { useLocation } from "preact-iso";

export function Header() {
  const { url } = useLocation();

  return (
    <header>
      <nav class='bg-amber-400'>
        <a href='/' class={url == "/" && "active"}>
          Home
        </a>
        <a href='/404' class={url == "/404" && "active"}>
          404
        </a>
      </nav>
    </header>
  );
}
