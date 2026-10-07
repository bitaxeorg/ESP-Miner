import { ComponentFixture, TestBed } from '@angular/core/testing';
import { NO_ERRORS_SCHEMA } from '@angular/core';
import { PoolComponent } from './pool.component';
import { FormsModule, ReactiveFormsModule } from '@angular/forms';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { provideRouter } from '@angular/router';
import { By } from '@angular/platform-browser';
import { SystemApiService } from 'src/app/services/system.service';
import { LiveDataService } from 'src/app/services/live-data.service';
import { of } from 'rxjs';
import { DropdownComponent } from '../dropdown/dropdown.component';
import { CheckboxComponent } from '../checkbox/checkbox.component';
import { TooltipDirective } from 'src/app/directives/tooltip.directive';
import { TooltipTextIconComponent } from '../tooltip-text-icon/tooltip-text-icon.component';

import { RadioButtonComponent } from '../radio-button/radio-button.component';

describe('PoolComponent', () => {
  let component: PoolComponent;
  let fixture: ComponentFixture<PoolComponent>;
  let systemServiceSpy: jasmine.SpyObj<SystemApiService>;
  let mockInfo$: any;

  beforeEach(() => {
    mockInfo$ = of({
      ASICModel: 'BM1366',
      primaryPoolIndex: 0,
      secondaryPoolIndex: 1,
      pools: [
        {
          id: 0,
          stratumProtocol: 'SV1',
          stratumURL: 'pool0.com',
          stratumPort: 3333,
          stratumUser: 'user0',
          stratumPassword: 'x',
          stratumSuggestedDifficulty: 0,
          stratumExtranonceSubscribe: false,
          stratumTLS: 0,
          stratumCert: '',
          stratumDecodeCoinbase: true,
          stratumV2ChannelType: 'extended',
          stratumV2AuthorityPubkey: '',
          stratumV2RequireAuth: false
        },
        {
          id: 1,
          stratumProtocol: 'SV1',
          stratumURL: 'pool1.com',
          stratumPort: 3333,
          stratumUser: 'user1',
          stratumPassword: 'x',
          stratumSuggestedDifficulty: 0,
          stratumExtranonceSubscribe: false,
          stratumTLS: 0,
          stratumCert: '',
          stratumDecodeCoinbase: true,
          stratumV2ChannelType: 'extended',
          stratumV2AuthorityPubkey: '',
          stratumV2RequireAuth: false
        }
      ]
    });

    const liveDataMock = {
      info$: mockInfo$
    };

    systemServiceSpy = jasmine.createSpyObj('SystemApiService', ['updateSystem', 'deletePool']);
    systemServiceSpy.updateSystem.and.returnValue(of({ status: 'success' }));

    TestBed.configureTestingModule({
      declarations: [
        PoolComponent,
        TooltipTextIconComponent
      ],
      imports: [
        FormsModule,
        ReactiveFormsModule,
        DropdownComponent,
        CheckboxComponent,
        RadioButtonComponent,
        TooltipDirective
      ],
      schemas: [NO_ERRORS_SCHEMA],
      providers: [
        provideHttpClient(),
        provideToastr(),
        provideRouter([]),
        { provide: LiveDataService, useValue: liveDataMock },
        { provide: SystemApiService, useValue: systemServiceSpy }
      ]
    });

    fixture = TestBed.createComponent(PoolComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create and initialize form with mock data', () => {
    expect(component).toBeTruthy();
    expect(component.form.get('primaryPoolIndex')?.value).toBe(0);
    expect(component.form.get('secondaryPoolIndex')?.value).toBe(1);
    expect(component.poolsArray.length).toBe(2);
  });

  it('should save both modified pool details and changed pool indices when updateSystem is called', () => {
    // 1. Change primary pool index to 1 (which swaps secondary to 0)
    component.form.get('primaryPoolIndex')?.setValue(1);
    fixture.detectChanges();

    expect(component.form.get('primaryPoolIndex')?.value).toBe(1);
    expect(component.form.get('secondaryPoolIndex')?.value).toBe(0);

    // 2. Change pool 1 details (e.g. stratumURL)
    const pool1Group = component.poolsArray.at(1);
    pool1Group.get('stratumURL')?.setValue('pool1-modified.com');
    pool1Group.get('stratumURL')?.markAsDirty();
    fixture.detectChanges();

    // 3. Call updateSystem
    component.updateSystem();

    expect(systemServiceSpy.updateSystem).toHaveBeenCalled();
    const callArgs = systemServiceSpy.updateSystem.calls.mostRecent().args;
    const updatePayload = callArgs[1];

    expect(updatePayload.primaryPoolIndex).toBe(1);
    expect(updatePayload.secondaryPoolIndex).toBe(0);
    expect(updatePayload.pools[1].stratumURL).toBe('pool1-modified.com');
  });

  it('should update form values when interacting through dropdown DOM elements and inputs', () => {
    // 1. Click primary dropdown to open
    const primaryDropdown = fixture.debugElement.query(By.css('#primaryPoolSelect'));
    const trigger = primaryDropdown.query(By.css('.cursor-pointer'));
    trigger.nativeElement.click();
    fixture.detectChanges();

    // 2. Select Option 2 (value: 1)
    const options = primaryDropdown.queryAll(By.css('li'));
    expect(options.length).toBe(2);
    options[1].nativeElement.click();
    fixture.detectChanges();

    expect(component.form.get('primaryPoolIndex')?.value).toBe(1);
    expect(component.form.get('secondaryPoolIndex')?.value).toBe(0);
    expect(component.form.dirty).toBe(true);

    // 3. Edit input in pool details
    const urlInput = fixture.debugElement.query(By.css('#stratumURL_0'));
    urlInput.nativeElement.value = 'pool0-new.com';
    urlInput.nativeElement.dispatchEvent(new Event('input'));
    fixture.detectChanges();

    expect(component.form.get('primaryPoolIndex')?.value).toBe(1);
    expect(component.poolsArray.at(0).get('stratumURL')?.value).toBe('pool0-new.com');

    // 4. Submit
    component.updateSystem();
    const callArgs = systemServiceSpy.updateSystem.calls.mostRecent().args;
    const payload = callArgs[1];
    expect(payload.primaryPoolIndex).toBe(1);
    expect(payload.pools[0].stratumURL).toBe('pool0-new.com');
  });

  it('should handle secondaryPoolIndex swap, mark controls dirty, and update previousPrim/previousSec on save', () => {
    // Initially primary = 0, secondary = 1
    expect(component['previousPrim']).toBe(0);
    expect(component['previousSec']).toBe(1);

    // Change secondary to 0 (should swap primary to 1 and mark it dirty)
    component.form.get('secondaryPoolIndex')?.setValue(0);
    fixture.detectChanges();

    expect(component.form.get('primaryPoolIndex')?.value).toBe(1);
    expect(component.form.get('secondaryPoolIndex')?.value).toBe(0);
    expect(component.form.get('primaryPoolIndex')?.dirty).toBe(true);
    expect(component['previousPrim']).toBe(1);
    expect(component['previousSec']).toBe(0);

    // Modify a pool detail
    component.poolsArray.at(0).get('stratumPort')?.setValue(4444);
    component.poolsArray.at(0).get('stratumPort')?.markAsDirty();

    // Call updateSystem
    component.updateSystem();

    const callArgs = systemServiceSpy.updateSystem.calls.mostRecent().args;
    const payload = callArgs[1];
    expect(payload.primaryPoolIndex).toBe(1);
    expect(payload.secondaryPoolIndex).toBe(0);
    expect(payload.pools[0].stratumPort).toBe(4444);

    // Verify onSaveSuccess updated previous values and reset dirty state
    expect(component['previousPrim']).toBe(1);
    expect(component['previousSec']).toBe(0);
    expect(component.form.pristine).toBe(true);
  });
});

