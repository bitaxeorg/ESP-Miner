import { Component, Input, forwardRef } from '@angular/core';
import { ControlValueAccessor, NG_VALUE_ACCESSOR } from '@angular/forms';

@Component({
  selector: 'app-slider',
  template: `
    <div class="flex items-center w-full my-2">
      <input
        type="range"
        [min]="min"
        [max]="max"
        [step]="step"
        [value]="value"
        [disabled]="disabled"
        (input)="onSliderInput($event)"
        [style.background]="getSliderBackground()"
        class="w-full h-1 rounded-lg appearance-none cursor-pointer focus:outline-none
               [&::-webkit-slider-runnable-track]:h-1
               [&::-webkit-slider-runnable-track]:bg-transparent
               [&::-webkit-slider-runnable-track]:rounded-full
               [&::-webkit-slider-thumb]:appearance-none
               [&::-webkit-slider-thumb]:h-4
               [&::-webkit-slider-thumb]:w-4
               [&::-webkit-slider-thumb]:rounded-full
               [&::-webkit-slider-thumb]:bg-slider-handle
               [&::-webkit-slider-thumb]:-mt-1.5
               [&::-moz-range-track]:h-1
               [&::-moz-range-track]:bg-transparent
               [&::-moz-range-track]:rounded-full
               [&::-moz-range-thumb]:border-none
               [&::-moz-range-thumb]:h-4
               [&::-moz-range-thumb]:w-4
               [&::-moz-range-thumb]:rounded-full
               [&::-moz-range-thumb]:bg-slider-handle"
      />
    </div>
  `,
  providers: [
    {
      provide: NG_VALUE_ACCESSOR,
      useExisting: forwardRef(() => SliderComponent),
      multi: true
    }
  ],
  standalone: true
})
export class SliderComponent implements ControlValueAccessor {
  @Input() min: number = 0;
  @Input() max: number = 100;
  @Input() step: number = 1;

  public value: number = 0;
  public disabled: boolean = false;

  private onChangeCallback: (_: any) => void = () => {};
  private onTouchedCallback: () => void = () => {};

  writeValue(value: any): void {
    if (value !== undefined && value !== null) {
      this.value = Number(value);
    }
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

  onSliderInput(event: Event) {
    const input = event.target as HTMLInputElement;
    this.value = Number(input.value);
    this.onChangeCallback(this.value);
    this.onTouchedCallback();
  }

  getSliderBackground(): string {
    const min = this.min ?? 0;
    const max = this.max ?? 100;
    const range = max - min;
    const percentage = range > 0 ? ((this.value - min) / range) * 100 : 0;
    return `linear-gradient(to right, var(--color-primary) 0%, var(--color-primary) ${percentage}%, var(--slider-bg, #dee2e6) ${percentage}%, var(--slider-bg, #dee2e6) 100%)`;
  }
}
