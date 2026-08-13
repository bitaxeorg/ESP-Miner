import { Component, ViewChild, AfterViewInit, OnDestroy, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormGroup, FormsModule } from '@angular/forms';
import { Observable, Subject, takeUntil } from 'rxjs';
import { EditComponent } from '../edit/edit.component';
import { DropdownComponent } from '../dropdown/dropdown.component';
import { I18nService, Locale } from 'src/app/i18n/i18n.service';
import { TranslatePipe } from 'src/app/i18n/translate.pipe';
import { SelectOption } from 'src/app/models/select-option.model';

@Component({
    selector: 'app-settings',
    templateUrl: './settings.component.html',
    standalone: true,
    imports: [CommonModule, FormsModule, EditComponent, DropdownComponent, TranslatePipe]
})
export class SettingsComponent implements AfterViewInit, OnInit, OnDestroy {
  form$!: Observable<FormGroup | null>;
  selectedLocale: Locale = 'en';
  private currentLocale: Locale = 'en';
  localeDirty = false;
  private localeDirtyMarksForm = false;
  private destroy$ = new Subject<void>();

  @ViewChild(EditComponent) editComponent!: EditComponent;

  constructor(private i18n: I18nService) {}

  ngOnInit() {
    this.currentLocale = this.i18n.locale;
    this.selectedLocale = this.currentLocale;
    this.i18n.locale$
      .pipe(takeUntil(this.destroy$))
      .subscribe(locale => {
        this.currentLocale = locale;
        if (!this.localeDirty) {
          this.selectedLocale = locale;
        }
        this.updateLocaleDirty();
      });
  }

  ngAfterViewInit() {
    this.form$ = this.editComponent.form$;
    if (this.localeDirty) {
      this.markFormDirtyForLocale();
    }
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }

  get languageOptions(): SelectOption<Locale>[] {
    return [
      { value: 'en', label: this.i18n.t('settings.ui.languages.en') },
      { value: 'de', label: this.i18n.t('settings.ui.languages.de') },
      { value: 'es', label: this.i18n.t('settings.ui.languages.es') },
    ];
  }

  onLanguageChange(locale: Locale) {
    this.selectedLocale = locale;
    this.updateLocaleDirty();
  }

  applyLocaleOnSave() {
    if (this.localeDirty) {
      this.i18n.setLocale(this.selectedLocale);
    }
  }

  private updateLocaleDirty() {
    const wasDirty = this.localeDirty;
    this.localeDirty = this.selectedLocale !== this.currentLocale;

    if (this.localeDirty && !wasDirty) {
      this.markFormDirtyForLocale();
    } else if (!this.localeDirty && wasDirty) {
      this.clearFormDirtyForLocale();
    }
  }

  private markFormDirtyForLocale() {
    const form = this.editComponent?.form;
    if (!form) return;

    this.localeDirtyMarksForm = !form.dirty;
    form.markAsDirty();
  }

  private clearFormDirtyForLocale() {
    const form = this.editComponent?.form;
    if (form && this.localeDirtyMarksForm && !Object.values(form.controls).some(control => control.dirty)) {
      form.markAsPristine();
    }
    this.localeDirtyMarksForm = false;
  }
}
