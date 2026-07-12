import { Component, ElementRef, EventEmitter, HostListener, Input, Output, forwardRef } from '@angular/core';
import { ControlValueAccessor, NG_VALUE_ACCESSOR } from '@angular/forms';
import { CommonModule } from '@angular/common';

export interface SelectOption {
  name?: string;
  label?: string;
  value: any;
}

@Component({
  selector: 'app-dropdown',
  template: `
    <div class="relative w-full" [id]="inputId">
      <div
        (click)="toggle($event)"
        [class.border-primary]="isOpen"
        [class.opacity-50]="disabled"
        [class.pointer-events-none]="disabled"
        [ngClass]="triggerClass || 'input-text'"
        class="flex items-center justify-between cursor-pointer select-none bg-bg-card border border-surface rounded outline-none transition duration-150"
      >
        <span>{{ selectedOptionName }}</span>
        <i class="pi pi-chevron-down text-xs transition-transform duration-200" [class.rotate-180]="isOpen"></i>
      </div>

      <ul
        *ngIf="isOpen"
        class="absolute z-50 left-0 w-full mt-1 max-h-60 overflow-y-auto bg-bg-card border border-surface rounded shadow-lg list-none p-0 m-0"
      >
        <li
          *ngFor="let option of options"
          (click)="select(option, $event)"
          class="px-3 py-2 cursor-pointer transition-colors duration-150 hover:bg-primary hover:text-white"
          [class.bg-bg-hover]="option.value === value"
          [class.text-primary]="option.value === value"
        >
          {{ option.name || option.label }}
        </li>
      </ul>
    </div>
  `,
  providers: [
    {
      provide: NG_VALUE_ACCESSOR,
      useExisting: forwardRef(() => DropdownComponent),
      multi: true
    }
  ],
  standalone: true,
  imports: [CommonModule]
})
export class DropdownComponent implements ControlValueAccessor {
  @Input() options: SelectOption[] = [];
  @Input() placeholder: string = 'Select...';
  @Input() inputId: string = '';
  @Input() triggerClass: string = '';

  @Output() onChange = new EventEmitter<any>();

  public value: any = null;
  public isOpen: boolean = false;
  public disabled: boolean = false;

  private onChangeCallback: (_: any) => void = () => {};
  private onTouchedCallback: () => void = () => {};

  constructor(private elementRef: ElementRef) {}

  get selectedOptionName(): string {
    const selected = this.options.find(opt => opt.value === this.value);
    if (!selected) return this.placeholder;
    return selected.name || selected.label || this.placeholder;
  }

  toggle(event: Event) {
    if (this.disabled) return;
    event.stopPropagation();
    this.isOpen = !this.isOpen;
  }

  select(option: SelectOption, event: Event) {
    event.stopPropagation();
    this.value = option.value;
    this.onChangeCallback(this.value);
    this.onTouchedCallback();
    this.onChange.emit({ value: this.value, originalEvent: event });
    this.isOpen = false;
  }

  @HostListener('document:click', ['$event'])
  onClickOutside(event: Event) {
    if (!this.elementRef.nativeElement.contains(event.target)) {
      this.isOpen = false;
    }
  }

  writeValue(value: any): void {
    this.value = value;
  }

  registerOnChange(fn: any): void {
    this.onChangeCallback = fn;
  }

  registerOnTouched(fn: any): void {
    this.onTouchedCallback = fn;
  }

  setDisabledState?(isDisabled: boolean): void {
    this.disabled = isDisabled;
  }
}
