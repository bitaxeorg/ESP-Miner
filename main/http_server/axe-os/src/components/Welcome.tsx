import { useEffect, useState } from "preact/hooks";

interface WelcomeProps {
  title?: string;
  subtitle?: string;
}

export function Welcome({
  title = "Welcome back",
  subtitle = "Monitor, manage and optimize your mining performance"
}: WelcomeProps) {
  const [currentTime, setCurrentTime] = useState(new Date());

  useEffect(() => {
    const timer = setInterval(() => {
      setCurrentTime(new Date());
    }, 1000);

    return () => clearInterval(timer);
  }, []);

  const getGreeting = () => {
    const hour = currentTime.getHours();
    if (hour < 12) return "Good morning";
    if (hour < 17) return "Good afternoon";
    return "Good evening";
  };

  return (
    <div class="mb-6 text-left pt-2">
      <h1 class="text-xl md:text-2xl font-bold text-slate-100 mb-1 text-left">
        {getGreeting()}
      </h1>
      <p class="text-slate-400 text-sm text-left">
        {subtitle}
      </p>
    </div>
  );
}