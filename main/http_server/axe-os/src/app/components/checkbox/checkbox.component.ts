import { Component, EventEmitter, Input, Output, forwardRef } from '@angular/core';
import { ControlValueAccessor, NG_VALUE_ACCESSOR } from '@angular/forms';

@Component({
  selector: 'app-checkbox',
  template: `
    <label class="inline-flex items-center cursor-pointer select-none">
      <input
        type="checkbox"
        [id]="inputId"
        [name]="name"
        [checked]="checked"
        [disabled]="disabled"
        (change)="onCheckboxChange($event)"
        class="hidden peer"
      />
      <div class="w-[18px] h-[18px] border border-checkbox-border bg-transparent hover:border-checkbox-hover flex items-center justify-center rounded-sm transition duration-150 peer-checked:bg-checkbox-bg peer-checked:border-checkbox-border">
        <i class="pi pi-check text-[10px] text-white opacity-0 peer-checked:opacity-100 transition-opacity duration-150"></i>
      </div>
    </label>
  `,
  styles: [`
    .peer:checked ~ div i {
      opacity: 1 !important;
    }
  `],
  providers: [
    {
      provide: NG_VALUE_ACCESSOR,
      useExisting: forwardRef(() => CheckboxComponent),
      multi: true
    }
  ],
  standalone: true
})
export class CheckboxComponent implements ControlValueAccessor {
  @Input() inputId: string = '';
  @Input() name: string = '';
  @Input() binary: boolean = true;

  @Output() onChange = new EventEmitter<any>();

  public checked: boolean = false;
  public disabled: boolean = false;

  private onChangeCallback: (_: any) => void = () => {};
  private onTouchedCallback: () => void = () => {};

  writeValue(value: any): void {
    this.checked = !!value;
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

  onCheckboxChange(event: Event) {
    const input = event.target as HTMLInputElement;
    this.checked = input.checked;
    this.onChangeCallback(this.checked);
    this.onTouchedCallback();
    this.onChange.emit({ checked: this.checked, originalEvent: event });
  }

  get model(): boolean {
    return this.checked;
  }
}
