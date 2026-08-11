import { Component, ElementRef, EventEmitter, HostListener, Input, Output, forwardRef } from '@angular/core';
import { ControlValueAccessor, NG_VALUE_ACCESSOR } from '@angular/forms';
import { CommonModule } from '@angular/common';
import { SelectOption } from '../../models/select-option.model';

@Component({
  selector: 'app-dropdown',
  template: `
    <div class="relative w-full" [id]="inputId">
      <div
        (click)="toggle($event)"
        [attr.tabindex]="disabled ? -1 : 0"
        [class.border-primary]="isOpen"
        [class.opacity-50]="disabled"
        [class.pointer-events-none]="disabled"
        [ngClass]="triggerClass || 'input-text'"
        class="flex items-center justify-between cursor-pointer select-none bg-bg-card border border-surface rounded outline-none transition duration-150 focus:border-primary"
      >
        <span>{{ selectedOptionName }}</span>
        <i class="pi pi-chevron-down text-xs transition-transform duration-200" [class.rotate-180]="isOpen"></i>
      </div>
      @if (isOpen) {
        <ul
          [class.bottom-full]="openUpward"
          [class.mb-1]="openUpward"
          [class.top-full]="!openUpward"
          [class.mt-1]="!openUpward"
          class="absolute z-50 left-0 w-full max-h-60 overflow-y-auto bg-bg-card border border-surface rounded shadow-lg list-none p-0 m-0"
        >
          @for (option of options; track option.value ?? $index; let i = $index) {
            <li
              (mouseenter)="focusedIndex = i"
              (mousedown)="$event.preventDefault()"
              (click)="select(option, $event)"
              class="px-3 py-2 cursor-pointer select-none"
              [class.bg-primary]="i === focusedIndex"
              [class.text-white]="i === focusedIndex"
              [class.bg-bg-hover]="i !== focusedIndex && option.value === value"
              [class.text-primary]="i !== focusedIndex && option.value === value"
            >
              {{ option.name || option.label }}
            </li>
          }
        </ul>
      }
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
  public openUpward: boolean = false;
  public disabled: boolean = false;
  public focusedIndex: number = -1;

  private onChangeCallback: (_: any) => void = () => {};
  private onTouchedCallback: () => void = () => {};

  constructor(private elementRef: ElementRef) {}

  get selectedOptionName(): string {
    const selected = this.options?.find(opt => opt.value === this.value);
    if (!selected) return this.placeholder;
    return selected.name || selected.label || this.placeholder;
  }

  trackByOption(index: number, option: SelectOption): any {
    return option.value ?? index;
  }

  private checkDirection() {
    if (!this.elementRef?.nativeElement) return;
    const rect = this.elementRef.nativeElement.getBoundingClientRect();
    const dropdownHeight = 240;
    const spaceBelow = window.innerHeight - rect.bottom;
    const spaceAbove = rect.top;

    this.openUpward = spaceBelow < dropdownHeight && spaceAbove > spaceBelow;
  }

  private scrollToFocused() {
    setTimeout(() => {
      if (!this.elementRef?.nativeElement || this.focusedIndex < 0) return;
      const list = this.elementRef.nativeElement.querySelector('ul');
      if (!list) return;
      const items = list.querySelectorAll('li');
      if (items && items[this.focusedIndex]) {
        items[this.focusedIndex].scrollIntoView({ block: 'nearest', inline: 'nearest' });
      }
    }, 0);
  }

  toggle(event: Event) {
    if (this.disabled) return;
    event.stopPropagation();
    this.isOpen = !this.isOpen;
    if (this.isOpen) {
      this.checkDirection();
      this.focusedIndex = this.options.findIndex(opt => opt.value === this.value);
      if (this.focusedIndex === -1 && this.options.length > 0) {
        this.focusedIndex = 0;
      }
      this.scrollToFocused();
    }
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

  @HostListener('keydown', ['$event'])
  handleKeyDown(event: KeyboardEvent) {
    if (this.disabled) return;

    switch (event.key) {
      case 'ArrowDown':
        event.preventDefault();
        if (!this.isOpen) {
          this.checkDirection();
          this.isOpen = true;
          this.focusedIndex = this.options.findIndex(opt => opt.value === this.value);
          if (this.focusedIndex === -1) this.focusedIndex = 0;
        } else {
          this.focusedIndex = (this.focusedIndex + 1) % this.options.length;
        }
        this.scrollToFocused();
        break;

      case 'ArrowUp':
        event.preventDefault();
        if (!this.isOpen) {
          this.checkDirection();
          this.isOpen = true;
          this.focusedIndex = this.options.findIndex(opt => opt.value === this.value);
          if (this.focusedIndex === -1) this.focusedIndex = this.options.length - 1;
        } else {
          this.focusedIndex = (this.focusedIndex - 1 + this.options.length) % this.options.length;
        }
        this.scrollToFocused();
        break;

      case 'Enter':
      case ' ':
        event.preventDefault();
        if (this.isOpen) {
          if (this.focusedIndex >= 0 && this.focusedIndex < this.options.length) {
            this.select(this.options[this.focusedIndex], event);
          } else {
            this.isOpen = false;
          }
        } else {
          this.checkDirection();
          this.isOpen = true;
          this.focusedIndex = this.options.findIndex(opt => opt.value === this.value);
          if (this.focusedIndex === -1) this.focusedIndex = 0;
          this.scrollToFocused();
        }
        break;

      case 'Escape':
        event.preventDefault();
        this.isOpen = false;
        break;
    }
  }

  @HostListener('focusout', ['$event'])
  onFocusOut(event: FocusEvent) {
    if (event.relatedTarget && !this.elementRef.nativeElement.contains(event.relatedTarget)) {
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
