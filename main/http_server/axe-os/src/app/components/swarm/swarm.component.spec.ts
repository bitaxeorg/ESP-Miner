import { ComponentFixture, TestBed } from '@angular/core/testing';

import { SwarmComponent } from './swarm.component';
import { ModalComponent } from '../modal/modal.component';
import { ReactiveFormsModule } from '@angular/forms';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { PrimeNGModule } from 'src/app/prime-ng.module';
import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { DiffSuffixPipe } from 'src/app/pipes/diff-suffix.pipe';
import { DateAgoPipe } from 'src/app/pipes/date-ago.pipe';

describe('SwarmComponent', () => {
  let component: SwarmComponent;
  let fixture: ComponentFixture<SwarmComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [
        SwarmComponent,
        ModalComponent
      ],
      imports: [
        PrimeNGModule,
        ReactiveFormsModule,
        HashSuffixPipe,
        DiffSuffixPipe,
        DateAgoPipe
      ],
      providers: [
        provideHttpClient(),
        provideToastr()
      ]
    });
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
});

