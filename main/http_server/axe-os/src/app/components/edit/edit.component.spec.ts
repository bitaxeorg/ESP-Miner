import { ComponentFixture, TestBed } from '@angular/core/testing';

import { EditComponent } from './edit.component';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { provideRouter } from '@angular/router';
import { FormControl, FormGroup } from '@angular/forms';
import { I18nService } from 'src/app/i18n/i18n.service';

describe('EditComponent', () => {
  let component: EditComponent;
  let fixture: ComponentFixture<EditComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [EditComponent],
      providers: [provideHttpClient(), provideToastr(), provideRouter([])]
    });
    fixture = TestBed.createComponent(EditComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  it('should not offer the fixed ST7789 display as a selectable option', () => {
    expect(component.displays).not.toContain('ST7789 (320x170)');
  });

  it('should treat a fixed ST7789 display as non-configurable', () => {
    component.form = new FormGroup({
      display: new FormControl('ST7789 (320x170)')
    });

    expect(component.isDisplayConfigurable).toBeFalse();
  });

  it('should offer localized language options', () => {
    const i18n = TestBed.inject(I18nService);

    i18n.setLocale('en');
    expect(component.languageOptions.map(o => o.label)).toEqual(['English', 'German', 'Spanish']);

    i18n.setLocale('de');
    expect(component.languageOptions.map(o => o.label)).toEqual(['Englisch', 'Deutsch', 'Spanisch']);

    i18n.setLocale('es');
    expect(component.languageOptions.map(o => o.label)).toEqual(['Inglés', 'Alemán', 'Español']);

    i18n.setLocale('en');
  });
});
