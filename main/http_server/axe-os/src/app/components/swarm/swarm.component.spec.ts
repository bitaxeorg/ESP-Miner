import { ComponentFixture, TestBed } from '@angular/core/testing';
import { FormsModule, ReactiveFormsModule } from '@angular/forms';
import { HttpClient, provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { of } from 'rxjs';

import { FormControl } from '@angular/forms';
import { addressValidator, SwarmComponent } from './swarm.component';
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
    // Idle: device3 (1 device)
    expect(component.totals.idleDevices).toBe(1);
    // Paused: device2 (1 device)
    expect(component.totals.pausedDevices).toBe(1);
    // Non-accessible: device5 (1 device)
    expect(component.totals.notAccessibleDevices).toBe(1);

    // Notifications
    expect(component.getDeviceNotification(device5)).toEqual({ color: 'red', msg: 'Not accessible' });
    expect(component.getDeviceNotification(device2)).toEqual({ color: 'yellow', msg: 'Paused' });
  });

  it('uses each peer\'s own presets for low and normal frequencies', () => {
    expect(component.getDeviceNotification({ frequency: 150, frequencyOptions: [100, 200] })).toBeUndefined();
    expect(component.getDeviceNotification({ frequency: 150, frequencyOptions: [200, 300] })?.msg).toBe('Frequency Low');
  });

  it('does not guess a minimum for a legacy peer or hide higher-priority faults', () => {
    expect(component.getDeviceNotification({ frequency: 100 })).toBeUndefined();
    expect(component.getDeviceNotification({ frequency: 0 })?.msg).toBe('Frequency Low');
    expect(component.getDeviceNotification({ frequency: 100, frequencyOptions: [200], overheat_mode: 1 })?.msg).toBe('Overheated');
    expect(component.getDeviceNotification({ frequency: 100, frequencyOptions: [200], miningPaused: true })?.msg).toBe('Paused');
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

  it('should reset notAccessible to false when a device responds successfully', () => {
    component.swarm = [{
      address: '192.168.1.104',
      connectionAddress: '192.168.1.104',
      notAccessible: true,
      hashRate: 0
    }];

    (component as any).getAllDeviceInfo(['192.168.1.104'], component.refreshErrorHandler, true).subscribe((result: any) => {
      const device = Array.isArray(result) ? result[0] : result;
      expect(device.notAccessible).toBeFalse();
    });
  });

  describe('addressValidator', () => {
    it('should allow valid IPv4 addresses', () => {
      expect(addressValidator(new FormControl('192.168.1.1'))).toBeNull();
      expect(addressValidator(new FormControl('10.0.0.1'))).toBeNull();
      expect(addressValidator(new FormControl('172.16.0.50'))).toBeNull();
    });

    it('should allow bare hostnames', () => {
      expect(addressValidator(new FormControl('bitaxe'))).toBeNull();
      expect(addressValidator(new FormControl('bitaxe-gamma-1'))).toBeNull();
      expect(addressValidator(new FormControl('miner01'))).toBeNull();
    });

    it('should allow hostnames with .local, .lan, .home.arpa, .internal, and other TLDs', () => {
      expect(addressValidator(new FormControl('bitaxe-1.local'))).toBeNull();
      expect(addressValidator(new FormControl('bitaxe-gamma-1.lan'))).toBeNull();
      expect(addressValidator(new FormControl('miner.home.arpa'))).toBeNull();
      expect(addressValidator(new FormControl('sub.corp.internal'))).toBeNull();
      expect(addressValidator(new FormControl('node-1.custom.domain'))).toBeNull();
    });

    it('should reject invalid addresses and hostnames', () => {
      expect(addressValidator(new FormControl('192.168.1.256'))).toEqual({ invalidAddress: true });
      expect(addressValidator(new FormControl('192.168.1'))).toEqual({ invalidAddress: true });
      expect(addressValidator(new FormControl('-bitaxe'))).toEqual({ invalidAddress: true });
      expect(addressValidator(new FormControl('bitaxe-'))).toEqual({ invalidAddress: true });
      expect(addressValidator(new FormControl('bitaxe..local'))).toEqual({ invalidAddress: true });
      expect(addressValidator(new FormControl('bitaxe space.lan'))).toEqual({ invalidAddress: true });
      expect(addressValidator(new FormControl('bitaxe_1.lan'))).toEqual({ invalidAddress: true });
    });

    it('should return null for empty input', () => {
      expect(addressValidator(new FormControl(''))).toBeNull();
      expect(addressValidator(new FormControl(null))).toBeNull();
    });
  });

  describe('getDeviceLink', () => {
    const mockDevice: any = {
      address: 'bitaxe-gamma-1.local',
      hostname: 'bitaxe-gamma-1',
      fullHostname: 'bitaxe-gamma-1.local',
      ipv4: '192.168.1.51',
      connectionAddress: '192.168.1.51'
    };

    it('should link via IP when dashboard is accessed via IP', () => {
      spyOn(component, 'getCurrentHostname').and.returnValue('192.168.1.50');
      expect(component.getDeviceLink(mockDevice)).toBe('192.168.1.51');
    });

    it('should link via .local when dashboard is accessed via .local', () => {
      spyOn(component, 'getCurrentHostname').and.returnValue('bitaxe-gamma-0.local');
      expect(component.getDeviceLink(mockDevice)).toBe('bitaxe-gamma-1.local');
    });

    it('should link via .lan when dashboard is accessed via .lan', () => {
      spyOn(component, 'getCurrentHostname').and.returnValue('bitaxe-gamma-0.lan');
      expect(component.getDeviceLink(mockDevice)).toBe('bitaxe-gamma-1.lan');
    });

    it('should link via .home.arpa when dashboard is accessed via .home.arpa', () => {
      spyOn(component, 'getCurrentHostname').and.returnValue('bitaxe-gamma-0.home.arpa');
      expect(component.getDeviceLink(mockDevice)).toBe('bitaxe-gamma-1.home.arpa');
    });

    it('should link via bare hostname when dashboard is accessed via bare hostname', () => {
      spyOn(component, 'getCurrentHostname').and.returnValue('bitaxe-gamma-0');
      expect(component.getDeviceLink(mockDevice)).toBe('bitaxe-gamma-1');
    });
  });
});
