import { Component, Injectable } from '@angular/core';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { Observable, Subject } from 'rxjs';

export interface DialogOption {
  label: string;
  rssi: number;
  value: string;
}

export interface DialogInstance {
  title: string;
  options: DialogOption[];
  selectSubject: Subject<string>;
}

@Injectable({
  providedIn: 'root'
})
export class DialogService {
  private activeDialogsSubject = new Subject<DialogInstance[]>();
  public activeDialogs$ = this.activeDialogsSubject.asObservable();
  
  private activeDialogs: DialogInstance[] = [];

  constructor() {}

  open(title: string, options: DialogOption[]): Observable<string> {
    const selectSubject = new Subject<string>();

    const instance: DialogInstance = {
      title,
      options,
      selectSubject
    };

    this.activeDialogs.push(instance);
    this.activeDialogsSubject.next([...this.activeDialogs]);

    selectSubject.subscribe({
      complete: () => {
        this.activeDialogs = this.activeDialogs.filter(d => d !== instance);
        this.activeDialogsSubject.next([...this.activeDialogs]);
      }
    });

    return selectSubject.asObservable();
  }
}

@Component({
    selector: 'app-dialog-list',
    template: `
    <app-modal *ngFor="let dialog of dialogs"
               [headline]="dialog.title"
               [closable]="true"
               (close)="closeDialog(dialog)"
               [isVisible]="true">
      <div class="flex flex-col gap-2 pt-2">
        <button *ngFor="let option of dialog.options"
                (click)="selectOption(dialog, option.value)"
                class="w-full text-left !flex items-center !justify-start btn btn-secondary px-4 py-3 gap-3"
                appTooltip="{{option.label}} ({{option.rssi}} dBm)"
                tooltipPosition="bottom"
        >
          <span>{{option.label}}</span>
          <wifi-icon [rssi]="option.rssi" />
        </button>
      </div>
    </app-modal>
  `,
    standalone: false
})
export class DialogListComponent {
  public dialogs: DialogInstance[] = [];

  constructor(private dialogService: DialogService) {
    this.dialogService.activeDialogs$.pipe(
      takeUntilDestroyed()
    ).subscribe(dialogs => {
      this.dialogs = dialogs;
    });
  }

  selectOption(dialog: DialogInstance, value: string) {
    dialog.selectSubject.next(value);
    dialog.selectSubject.complete();
  }

  closeDialog(dialog: DialogInstance) {
    dialog.selectSubject.next('');
    dialog.selectSubject.complete();
  }
}
