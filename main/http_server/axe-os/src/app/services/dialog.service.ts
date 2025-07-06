import { Component, Injectable } from '@angular/core';
import { Observable, Subject } from 'rxjs';
import { DialogService as PrimeDialogService, DynamicDialogConfig } from 'primeng/dynamicdialog';

interface DialogOption {
  label: string;
  rssi: number;
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
      width: '500px',
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
    <div class="flex flex-column gap-2">
      <p-button *ngFor="let option of config.data.options"
        [label]="option.label"
        (onClick)="config.data.onSelect(option.value)"
        styleClass="w-full text-left flex align-items-baseline"
        pTooltip="{{option.label}} ({{option.rssi}}dBm)"
        tooltipPosition="bottom"
      >
        <figure class="wifi-icon flex-order-2"
          [ngClass]="{
            'wifi-icon--excellent': option.rssi > -50,
            'wifi-icon--good': option.rssi <= -50 && option.rssi > -60,
            'wifi-icon--fair': option.rssi <= -60 && option.rssi > -70,
            'wifi-icon--weak': option.rssi <= -70
          }">
          <i *ngFor="let item of [1,2,3,4]"></i>
        </figure>
      </p-button>
    </div>
  `
})
export class DialogListComponent {
  constructor(public config: DynamicDialogConfig) {}
}
