import { Component, Injectable } from '@angular/core';
import { Observable, Subject } from 'rxjs';
import { DialogService as PrimeDialogService, DynamicDialogConfig } from 'primeng/dynamicdialog';

interface DialogOption {
  label: string;
  value: string;
}

@Injectable({
  providedIn: 'root'
})
export class DialogService {
  constructor(private primeDialogService: PrimeDialogService) {}

  open(title: string, options: DialogOption[]): Observable<string> {
    const result = new Subject<string>();

    const ref = this.primeDialogService.open(DialogListComponent, {
      header: title,
      width: '400px',
      data: {
        options: options,
        onSelect: (value: string) => {
          result.next(value);
          ref.close();
        }
      }
    });

    ref.onClose.subscribe(() => {
      result.complete();
    });

    return result.asObservable();
  }
}

@Component({
  template: `
    <style>
      ::ng-deep .p-button:focus {
        box-shadow: none !important;
        background-color: var(--primary-color) !important;
      }
    </style>
    <div class="flex flex-column gap-2">
      <p-button *ngFor="let option of config.data.options"
        [label]="option.label"
        (onClick)="config.data.onSelect(option.value)"
        styleClass="w-full text-left"
      ></p-button>
    </div>
  `,
   standalone: false
})
export class DialogListComponent {
  constructor(public config: DynamicDialogConfig) {}
}
