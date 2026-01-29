import { Component, ViewChild, AfterViewInit } from '@angular/core';
import { AsyncPipe } from '@angular/common';
import { FormGroup } from '@angular/forms';
import { Observable } from 'rxjs';
import { EditComponent } from '../edit/edit.component';
import { TranslatePipe } from 'src/app/i18n/translate.pipe';

@Component({
    selector: 'app-settings',
    templateUrl: './settings.component.html',
    standalone: true,
    imports: [AsyncPipe, EditComponent, TranslatePipe]
})
export class SettingsComponent implements AfterViewInit {
  form$!: Observable<FormGroup | null>;

  @ViewChild(EditComponent) editComponent!: EditComponent;

  constructor() {}

  ngAfterViewInit() {
    this.form$ = this.editComponent.form$;
  }
}
