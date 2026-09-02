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
