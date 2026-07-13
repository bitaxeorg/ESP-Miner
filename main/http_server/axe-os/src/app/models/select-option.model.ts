export interface SelectOption<T = any> {
  label?: string;
  name?: string;
  value: T;
  disabled?: boolean;
}
