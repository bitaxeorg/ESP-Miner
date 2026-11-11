import { Component, ViewChild, AfterViewInit } from '@angular/core';
import { AsyncPipe } from '@angular/common';
import { FormGroup } from '@angular/forms';
import { Observable } from 'rxjs';
import { EditComponent } from '../edit/edit.component';
import { WebhookAlertsComponent } from '../webhook-alerts/webhook-alerts.component';

@Component({
    selector: 'app-settings',
    templateUrl: './settings.component.html',
    standalone: true,
    imports: [AsyncPipe, EditComponent, WebhookAlertsComponent]
})
export class SettingsComponent implements AfterViewInit {
  form$!: Observable<FormGroup | null>;

  @ViewChild(EditComponent) editComponent!: EditComponent;

  constructor() {}

  ngAfterViewInit() {
    this.form$ = this.editComponent.form$;
  }
}
