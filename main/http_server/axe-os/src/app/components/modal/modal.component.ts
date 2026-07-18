import { Component, Input, Output, EventEmitter } from '@angular/core';

@Component({
    selector: 'app-modal',
    templateUrl: './modal.component.html',
    styleUrls: ['./modal.component.scss'],
    standalone: false
})
export class ModalComponent {
  private _isVisible = false;

  @Input()
  get isVisible(): boolean {
    return this._isVisible;
  }
  set isVisible(val: boolean) {
    if (this._isVisible !== val) {
      this._isVisible = val;
      if (!val) {
        this.close.emit();
      }
    }
  }

  @Input() headline: string = '';
  @Input() closable: boolean = true;

  @Output() close = new EventEmitter<void>();

  constructor() {}
}

