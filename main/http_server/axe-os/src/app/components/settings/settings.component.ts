import { Component, ViewChild, AfterViewInit, OnDestroy, OnInit } from '@angular/core';
import { FormGroup } from '@angular/forms';
import { Observable, Subject, takeUntil } from 'rxjs';
import { EditComponent } from '../edit/edit.component';
import { I18nService, Locale } from 'src/app/i18n/i18n.service';

type LanguageOption = {
  label: string;
  value: Locale;
};

@Component({
  selector: 'app-settings',
  templateUrl: './settings.component.html',
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
    this.localeDirty = false;
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

  get languageOptions(): LanguageOption[] {
    const t = (key: string) => this.i18n.t(key);
    return [
      { value: 'en', label: t('settings.ui.languages.en') },
      { value: 'de', label: t('settings.ui.languages.de') },
      { value: 'es', label: t('settings.ui.languages.es') }
    ];
  }

  onLanguageChange(locale: Locale) {
    this.selectedLocale = locale;
    this.updateLocaleDirty();
  }

  applyLocaleOnSave() {
    if (!this.localeDirty) {
      return;
    }
    this.currentLocale = this.selectedLocale;
    this.updateLocaleDirty();
    this.i18n.setLocale(this.selectedLocale);
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
    if (!form) {
      return;
    }
    if (!form.dirty) {
      this.localeDirtyMarksForm = true;
      form.markAsDirty();
    } else {
      this.localeDirtyMarksForm = false;
    }
  }

  private clearFormDirtyForLocale() {
    const form = this.editComponent?.form;
    if (!form) {
      this.localeDirtyMarksForm = false;
      return;
    }

    if (this.localeDirtyMarksForm && !this.hasDirtyControl(form)) {
      form.markAsPristine();
    }
    this.localeDirtyMarksForm = false;
  }

  private hasDirtyControl(form: FormGroup): boolean {
    return Object.values(form.controls).some(control => control.dirty);
  }
}
