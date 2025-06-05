import { useEffect, useRef } from "preact/hooks";

interface TerminalProps {
  logs: string[];
  className?: string;
  autoScroll?: boolean;
}

export function Terminal({ logs, className = "", autoScroll = true }: TerminalProps) {
  const terminalRef = useRef<HTMLPreElement>(null);

  // Auto-scroll to bottom when logs change
  useEffect(() => {
    if (autoScroll && terminalRef.current) {
      terminalRef.current.scrollTop = terminalRef.current.scrollHeight;
    }
  }, [logs, autoScroll]);

  return (
    <div className={`w-full bg-black text-green-400 rounded-md p-4 font-mono text-sm ${className}`}>
      <pre ref={terminalRef} className='h-full w-full overflow-auto whitespace-pre-wrap'>
        {logs.length > 0 ? logs.join("\n") : "No logs to display"}
      </pre>
    </div>
  );
}
