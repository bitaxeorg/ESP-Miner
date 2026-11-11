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
      imports: [
        PoolComponent,
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

  it('should create and initialize form with mock data in priority order', () => {
    expect(component).toBeTruthy();
    expect(component.form.get('primaryPoolIndex')?.value).toBe(0);
    expect(component.form.get('secondaryPoolIndex')?.value).toBe(1);
    expect(component.poolsArray.length).toBe(2);
  });

  it('should save modified pool details with position-as-priority', () => {
    const pool0Group = component.poolsArray.at(0);
    pool0Group.get('stratumURL')?.setValue('pool0-modified.com');
    pool0Group.get('stratumURL')?.markAsDirty();
    fixture.detectChanges();

    component.updateSystem();

    expect(systemServiceSpy.updateSystem).toHaveBeenCalled();
    const callArgs = systemServiceSpy.updateSystem.calls.mostRecent().args;
    const updatePayload = callArgs[1];

    expect(updatePayload.primaryPoolIndex).toBe(0);
    expect(updatePayload.secondaryPoolIndex).toBe(1);
    expect(updatePayload.pools[0].stratumURL).toBe('pool0-modified.com');
  });

  it('should correctly determine whether advanced options should be displayed', () => {
    // SV1 pool should have advanced options
    expect(component.hasAdvancedOptions(0)).toBe(true);

    // Switch pool 0 to SV2 Standard channel -> should NOT have advanced options
    component.poolsArray.at(0).get('stratumProtocol')?.setValue('SV2');
    component.poolsArray.at(0).get('stratumV2ChannelType')?.setValue('standard');
    expect(component.hasAdvancedOptions(0)).toBe(false);

    // Switch pool 0 to SV2 Extended channel -> should have advanced options
    component.poolsArray.at(0).get('stratumV2ChannelType')?.setValue('extended');
    expect(component.hasAdvancedOptions(0)).toBe(true);
  });

  it('should reorder pools using move buttons, swapping slots and keeping slot 0 as primary', () => {
    // Initially: pool 0 is 'pool0.com' (Primary), pool 1 is 'pool1.com' (Fallback)
    expect(component.poolsArray.at(0).get('stratumURL')?.value).toBe('pool0.com');
    expect(component.poolsArray.at(0).get('id')?.value).toBe(0);
    expect(component.poolsArray.at(1).get('stratumURL')?.value).toBe('pool1.com');
    expect(component.poolsArray.at(1).get('id')?.value).toBe(1);

    // Moving index 0 up should be a no-op
    component.movePoolUp(0);
    expect(component.poolsArray.at(0).get('stratumURL')?.value).toBe('pool0.com');

    // Moving index 1 down should be a no-op
    component.movePoolDown(1);
    expect(component.poolsArray.at(1).get('stratumURL')?.value).toBe('pool1.com');

    // Move pool 0 down (swaps with pool 1)
    component.movePoolDown(0);

    // Now index 0 is 'pool1.com' with id 0 (new Primary!), index 1 is 'pool0.com' with id 1 (new Fallback!)
    expect(component.poolsArray.at(0).get('stratumURL')?.value).toBe('pool1.com');
    expect(component.poolsArray.at(0).get('id')?.value).toBe(0);
    expect(component.poolsArray.at(1).get('stratumURL')?.value).toBe('pool0.com');
    expect(component.poolsArray.at(1).get('id')?.value).toBe(1);

    expect(component.form.get('primaryPoolIndex')?.value).toBe(0);
    expect(component.form.get('secondaryPoolIndex')?.value).toBe(1);
    expect(component.form.dirty).toBe(true);

    // Move pool 1 back up
    component.movePoolUp(1);
    expect(component.poolsArray.at(0).get('stratumURL')?.value).toBe('pool0.com');
    expect(component.poolsArray.at(0).get('id')?.value).toBe(0);
    expect(component.poolsArray.at(1).get('stratumURL')?.value).toBe('pool1.com');
    expect(component.poolsArray.at(1).get('id')?.value).toBe(1);
  });

  it('should handle pool deletion, shift remaining pools, and prevent deleting only pool', () => {
    expect(component.isDeleteDisabled()).toBe(false);

    // Delete pool at index 0 (which was pool0.com)
    component.deletePool(0);
    fixture.detectChanges();

    // Now only 1 pool remains (which is pool1.com, renumbered to id 0)
    expect(component.poolsArray.length).toBe(1);
    expect(component.poolsArray.at(0).get('stratumURL')?.value).toBe('pool1.com');
    expect(component.poolsArray.at(0).get('id')?.value).toBe(0);
    expect(component.form.get('primaryPoolIndex')?.value).toBe(0);
    expect(component.form.get('secondaryPoolIndex')?.value).toBe(0);

    // Delete should now be disabled because only 1 pool remains
    expect(component.isDeleteDisabled()).toBe(true);

    // Trying to delete the only pool should be rejected
    component.deletePool(0);
    expect(component.poolsArray.length).toBe(1);
  });
});

