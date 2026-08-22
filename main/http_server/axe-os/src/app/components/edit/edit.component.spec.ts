import { ComponentFixture, TestBed } from '@angular/core/testing';

import { EditComponent } from './edit.component';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { provideRouter } from '@angular/router';
import { FormControl, FormGroup } from '@angular/forms';

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
});
