import { ComponentFixture, fakeAsync, TestBed, tick } from '@angular/core/testing';

import { EditComponent } from './edit.component';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { provideRouter } from '@angular/router';
import { FormControl, FormGroup } from '@angular/forms';
import { of } from 'rxjs';
import { SystemApiService } from 'src/app/services/system.service';

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

  it('should load persisted fan mode directly from the system API', fakeAsync(() => {
    const systemService = TestBed.inject(SystemApiService);
    const getInfo = spyOn(systemService, 'getInfo').and.returnValue(of({
      display: 'NONE',
      rotation: 0,
      invertscreen: 0,
      displayTimeout: -1,
      coreVoltage: 1150,
      frequency: 500,
      autofanspeed: 0,
      minFanSpeed: 25,
      manualFanSpeed: 65,
      temptarget: 60,
      overheat_mode: 0,
      statsFrequency: 30,
      statsLimit: 720,
      overclockEnabled: 0
    } as any));
    spyOn(systemService, 'getAsicSettings').and.returnValue(of({
      defaultFrequency: 500,
      frequencyOptions: [500],
      defaultVoltage: 1150,
      voltageOptions: [1150]
    } as any));

    component.ngOnInit();
    tick();

    expect(getInfo).toHaveBeenCalledWith('');
    expect(component.form.controls['autofanspeed'].value).toBeFalse();
    expect(component.form.controls['manualFanSpeed'].value).toBe(65);
  }));
});
