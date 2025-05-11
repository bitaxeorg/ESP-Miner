import { ComponentChildren } from "preact";

interface TableProps {
  children: ComponentChildren;
  className?: string;
}

export function Table({ children, className = "" }: TableProps) {
  return (
    <div className={`w-full overflow-auto ${className}`}>
      <table className='w-full caption-bottom text-sm'>{children}</table>
    </div>
  );
}

interface TableHeaderProps {
  children: ComponentChildren;
  className?: string;
}

export function TableHeader({ children, className = "" }: TableHeaderProps) {
  return <thead className={`border-b border-border bg-muted ${className}`}>{children}</thead>;
}

interface TableBodyProps {
  children: ComponentChildren;
  className?: string;
}

export function TableBody({ children, className = "" }: TableBodyProps) {
  return <tbody className={`${className}`}>{children}</tbody>;
}

interface TableRowProps {
  children: ComponentChildren;
  className?: string;
}

export function TableRow({ children, className = "" }: TableRowProps) {
  return (
    <tr className={`border-b border-border transition-colors hover:bg-muted/50 ${className}`}>
      {children}
    </tr>
  );
}

interface TableHeadProps {
  children: ComponentChildren;
  className?: string;
}

export function TableHead({ children, className = "" }: TableHeadProps) {
  return (
    <th
      className={`h-12 px-4 text-left align-middle font-medium text-muted-foreground ${className}`}
    >
      {children}
    </th>
  );
}

interface TableCellProps {
  children: ComponentChildren;
  className?: string;
  colSpan?: number;
}

export function TableCell({ children, className = "", colSpan }: TableCellProps) {
  return (
    <td colSpan={colSpan} className={`p-4 align-middle ${className}`}>
      {children}
    </td>
  );
}
