import { Component, EventEmitter, Input, Output, forwardRef } from '@angular/core';
import { ControlValueAccessor, NG_VALUE_ACCESSOR } from '@angular/forms';

@Component({
  selector: 'app-radio-button',
  template: `
    <label class="inline-flex items-center cursor-pointer select-none">
      <input
        type="radio"
        [id]="inputId"
        [name]="name"
        [value]="value"
        [checked]="checked"
        [disabled]="disabled"
        (change)="onRadioChange($event)"
        class="hidden peer"
      />
      <div class="w-[18px] h-[18px] rounded-full border border-checkbox-border hover:border-checkbox-hover bg-transparent flex items-center justify-center transition duration-150 peer-checked:border-checkbox-border">
        <div class="w-[10px] h-[10px] rounded-full bg-checkbox-bg opacity-0 peer-checked:opacity-100 transition-opacity duration-150"></div>
      </div>
    </label>
  `,
  styles: [`
    .peer:checked ~ div div {
      opacity: 1 !important;
    }
  `],
  providers: [
    {
      provide: NG_VALUE_ACCESSOR,
      useExisting: forwardRef(() => RadioButtonComponent),
      multi: true
    }
  ],
  standalone: true
})
export class RadioButtonComponent implements ControlValueAccessor {
  @Input() inputId: string = '';
  @Input() name: string = '';
  @Input() value: any;

  @Output() onClick = new EventEmitter<any>();

  public checked: boolean = false;
  public disabled: boolean = false;

  private onChangeCallback: (_: any) => void = () => {};
  private onTouchedCallback: () => void = () => {};

  writeValue(value: any): void {
    this.checked = (value === this.value);
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

  onRadioChange(event: Event) {
    this.checked = true;
    this.onChangeCallback(this.value);
    this.onTouchedCallback();
    this.onClick.emit(event);
  }
}
