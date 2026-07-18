import { Component, ViewChild, AfterViewInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormGroup } from '@angular/forms';
import { Observable } from 'rxjs';
import { EditComponent } from '../edit/edit.component';
import { SecurityComponent } from '../security/security.component';

@Component({
    selector: 'app-settings',
    templateUrl: './settings.component.html',
    standalone: true,
    imports: [CommonModule, EditComponent, SecurityComponent]
})
export class SettingsComponent implements AfterViewInit {
  form$!: Observable<FormGroup | null>;

  @ViewChild(EditComponent) editComponent!: EditComponent;

  constructor() {}

  ngAfterViewInit() {
    this.form$ = this.editComponent.form$;
  }
}
