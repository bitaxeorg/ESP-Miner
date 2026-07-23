import { ComponentFixture, TestBed } from '@angular/core/testing';
import { FormsModule, ReactiveFormsModule } from '@angular/forms';
import { HttpClient, provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { of } from 'rxjs';

import { SwarmComponent } from './swarm.component';
import { ModalComponent } from '../modal/modal.component';
import { TooltipTextIconComponent } from 'src/app/components/tooltip-text-icon/tooltip-text-icon.component';
import { DropdownComponent } from 'src/app/components/dropdown/dropdown.component';
import { SliderComponent } from 'src/app/components/slider/slider.component';
import { TooltipDirective } from 'src/app/directives/tooltip.directive';

import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { DiffSuffixPipe } from 'src/app/pipes/diff-suffix.pipe';
import { DateAgoPipe } from 'src/app/pipes/date-ago.pipe';
import { AddressPipe } from 'src/app/pipes/address.pipe';
import { SatsPipe } from 'src/app/pipes/sats.pipe';

describe('SwarmComponent', () => {
  let component: SwarmComponent;
  let fixture: ComponentFixture<SwarmComponent>;
  let httpClient: HttpClient;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [
        SwarmComponent,
        ModalComponent,
        TooltipTextIconComponent
      ],
      imports: [
        ReactiveFormsModule,
        FormsModule,
        TooltipDirective,
        DropdownComponent,
        SliderComponent,
        HashSuffixPipe,
        DiffSuffixPipe,
        DateAgoPipe,
        AddressPipe,
        SatsPipe
      ],
      providers: [
        provideHttpClient(),
        provideToastr()
      ]
    });
    
    httpClient = TestBed.inject(HttpClient);
    spyOn(httpClient, 'get').and.callFake(((url: string) => {
      if (url.includes('/api/system/info')) {
        return of({ ipv4: '192.168.1.1', version: 'v2.1.2' });
      }
      return of({});
    }) as any);

    fixture = TestBed.createComponent(SwarmComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  it('should identify non-hashing devices and calculate totals correctly', () => {
    const device1 = { IP: '192.168.1.100', hashRate: 1500, power: 120, bestDiff: 2000, miningPaused: false };
    const device2 = { IP: '192.168.1.101', hashRate: 1600, power: 130, bestDiff: 2500, miningPaused: true }; // paused
    const device3 = { IP: '192.168.1.102', hashRate: 0, power: 10, bestDiff: 0, miningPaused: false }; // not hashing (0 hashrate)
    const device4 = { IP: '192.168.1.103', hashRate: 1800, power: 140, bestDiff: 3000, miningPaused: false }; // hashing
    const device5 = { IP: '192.168.1.104', hashRate: 0, power: 0, bestDiff: 0, miningPaused: false, notAccessible: true }; // non-accessible

    component.swarm = [device1, device2, device3, device4, device5];

    // Check isNotHashing
    expect(component.isNotHashing(device1)).toBeFalse();
    expect(component.isNotHashing(device2)).toBeTrue();
    expect(component.isNotHashing(device3)).toBeTrue();
    expect(component.isNotHashing(device4)).toBeFalse();
    expect(component.isNotHashing(device5)).toBeTrue();

    // Call calculateTotals
    component['calculateTotals']();

    // Totals should only sum device1 and device4:
    // Hashrate: 1500 + 1800 = 3300
    // Power: 120 + 140 = 260
    // BestDiff: max(2000, 3000) = 3000 (excluding 2500 from device2)
    expect(component.totals.hashRate).toBe(3300);
    expect(component.totals.power).toBe(260);
    expect(component.totals.bestDiff).toBe(3000);

    // Hashing: device1, device4 (2 devices)
    expect(component.totals.hashingDevices).toBe(2);
    // Paused: device2 (1 device)
    expect(component.totals.pausedDevices).toBe(1);
    // Non-accessible: device5 (1 device)
    expect(component.totals.notAccessibleDevices).toBe(1);
  });

  it('should render swarm list details and custom components when devices are present', () => {
    component.swarm = [
      {
        address: 'bitaxe-1.local',
        displayName: 'Bitaxe 1',
        connectionAddress: '192.168.1.100',
        ASICModel: 'BM1366',
        deviceModel: 'Ultra',
        swarmColor: 'purple',
        asicCount: 1,
        hashRate: 500e9,
        sharesAccepted: 100,
        sharesRejected: 1,
        bestDiff: 1000,
        bestSessionDiff: 500,
        power: 15,
        temp: 55,
        version: 'v2.1.2',
        uptimeSeconds: 3600,
        poolDifficulty: 1000
      }
    ];
    fixture.detectChanges();

    const element = fixture.nativeElement;
    
    // Verify that components inside *ngIf are rendered
    expect(element.querySelector('app-slider')).toBeTruthy();
    expect(element.querySelector('app-modal')).toBeTruthy();
  });
});
