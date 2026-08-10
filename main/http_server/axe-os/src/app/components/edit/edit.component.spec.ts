import { ComponentFixture, TestBed } from '@angular/core/testing';
import { FormGroup, FormControl, Validators } from '@angular/forms';

import { EditComponent } from './edit.component';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { provideRouter } from '@angular/router';

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

  // Helper: create a minimal form with just the carousel controls so the
  // toggleCarouselScreen helper can be tested without a full loadDeviceSettings()
  // round-trip (which requires a live backend the test env doesn't have).
  function setupCarouselForm(mask: number = 15, delay: number = 10) {
    component.form = new FormGroup({
      carouselScreens: new FormControl(mask, [Validators.required, Validators.min(1), Validators.max(15)]),
      carouselDelay: new FormControl(delay, [Validators.required, Validators.min(1), Validators.max(60)]),
    });
  }

  it('toggleCarouselScreen should set the correct bit', () => {
    setupCarouselForm(0b0001);

    component.toggleCarouselScreen(1, true);
    expect(component.form.controls['carouselScreens'].value).toBe(0b0011);

    component.toggleCarouselScreen(0, false);
    expect(component.form.controls['carouselScreens'].value).toBe(0b0010);
  });

  it('toggleCarouselScreen should refuse to clear the last enabled screen', () => {
    setupCarouselForm(0b0100);

    component.toggleCarouselScreen(2, false);
    expect(component.form.controls['carouselScreens'].value).toBe(0b0100);
  });

  it('carouselScreens and carouselDelay should be in noRestartFields', () => {
    expect(component.noRestartFields).toContain('carouselScreens');
    expect(component.noRestartFields).toContain('carouselDelay');
  });
});
