import { Component, ViewChild, AfterViewInit } from '@angular/core';
import { FormGroup } from '@angular/forms';
import { Observable } from 'rxjs';
import { EditComponent } from '../edit/edit.component';

@Component({
  selector: 'app-settings',
  templateUrl: './settings.component.html',
})
export class SettingsComponent implements AfterViewInit {
  form$!: Observable<FormGroup | null>;

  @ViewChild(EditComponent) editComponent!: EditComponent;

  constructor() {}

  ngAfterViewInit() {
    this.form$ = this.editComponent.form$;
  }
}
