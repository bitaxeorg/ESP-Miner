import 'chartjs-adapter-moment';
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { HomeComponent } from './home.component';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { ReactiveFormsModule, FormsModule } from '@angular/forms';
import { NoopAnimationsModule } from '@angular/platform-browser/animations';
import { Title } from '@angular/platform-browser';
import { provideRouter } from '@angular/router';
import { AppChartComponent } from 'src/app/components/chart/app-chart.component';
import { TooltipDirective } from 'src/app/directives/tooltip.directive';
import { DropdownComponent } from 'src/app/components/dropdown/dropdown.component';
import { BehaviorSubject, of } from 'rxjs';

import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { DiffSuffixPipe } from 'src/app/pipes/diff-suffix.pipe';
import { DateAgoPipe } from 'src/app/pipes/date-ago.pipe';
import { AddressPipe } from 'src/app/pipes/address.pipe';
import { SatsPipe } from 'src/app/pipes/sats.pipe';
import { ByteSuffixPipe } from 'src/app/pipes/byte-suffix.pipe';

import { TooltipTextIconComponent } from 'src/app/components/tooltip-text-icon/tooltip-text-icon.component';
import { TooltipIconComponent } from 'src/app/components/tooltip-icon/tooltip-icon.component';
import { ConfettiComponent } from 'src/app/components/confetti/confetti.component';
import { SnowflakesComponent } from 'src/app/components/snowflakes/snowflakes.component';

import { SystemApiService } from 'src/app/services/system.service';
import { LiveDataService } from 'src/app/services/live-data.service';
import { ThemeService } from 'src/app/services/theme.service';
import { QuicklinkService } from 'src/app/services/quicklink.service';
import { LoadingService } from 'src/app/services/loading.service';
import { ShareRejectionExplanationService } from 'src/app/services/share-rejection-explanation.service';
import { LocalStorageService } from 'src/app/local-storage.service';
import { DashboardEditService } from 'src/app/services/dashboard-edit.service';
import { LayoutService } from 'src/app/layout/service/app.layout.service';
import { SystemInfo as ISystemInfo, SystemStatistics as ISystemStatistics } from 'src/app/generated/models';

const mockSystemInfo: ISystemInfo = {
  power_fault: '',
  blockFound: 0,
  sharesAccepted: 100,
  sharesRejected: 0,
  bestDiff: 1200000000,
  bestSessionDiff: 500000000,
  uptimeSeconds: 3600,
  hashRate: 500,
  hashRate_1m: 500,
  hashRate_10m: 500,
  hashRate_1h: 500,
  temp: 55,
  temp2: 0,
  vrTemp: 0,
  fanspeed: 80,
  fanrpm: 3000,
  fan2rpm: 0,
  power: 15,
  voltage: 5.0,
  nominalVoltage: 5.0,
  actualFrequency: 600,
  coreVoltageActual: 1.2,
  current: 0,
  coreVoltage: 0,
  maxPower: 20,
  poolConnectionInfo: 'Connected',
  responseTime: 45,
  responseShareBatch: 1,
  poolDifficulty: 1000,
  blockHeight: 800000,
  networkDifficulty: 5000000,
  scriptsig: 'test-scriptsig',
  coinbaseOutputs: [],
  coinbaseValueTotalSatoshis: 625000000,
  hashrateMonitor: {
    asics: [
      {
        errorCount: 0,
        domains: [500],
        total: 500
      }
    ]
  },
  showNewBlock: false,
  version: 'v2.1.2',
  boardVersion: 'v2.2',
  ssid: 'test-ssid',
  wifiStatus: 'Connected',
  wifiRSSI: -45,
  ipv4: '192.168.1.100',
  ipv6: 'fe80::1',
  macAddr: '00:11:22:33:44:55',
  cpuUsage: 12.5,
  freeHeap: 100000,
  freeHeapInternal: 50000,
  freeHeapSpiram: 50000,
  minFreeHeap: 40000,
  maxAllocHeap: 30000,
  axeOSVersion: 'v1.0.0',
  idfVersion: 'v5.1.0',
  stratumURL: 'stratum.pool.com',
  stratumUser: 'worker.name',
  stratumPort: 3333,
  stratumProtocol: 'SV1',
  fallbackStratumURL: 'fallback.pool.com',
  fallbackStratumUser: 'worker.fallback',
  fallbackStratumPort: 3333,
  fallbackStratumProtocol: 'SV1',
  isUsingFallbackStratum: false
} as any;

const mockSystemStatistics: ISystemStatistics = {
  labels: ['timestamp', 'hashrate', 'power'],
  statistics: [
    [Date.now() - 60000, 500, 15],
    [Date.now(), 500, 15]
  ],
  currentTimestamp: Date.now()
} as any;

const mockLiveDataService = {
  info$: new BehaviorSubject<ISystemInfo>(mockSystemInfo),
  connected$: of(true)
};

const mockSystemApiService = {
  getStatistics: () => of(mockSystemStatistics),
  updateSystem: () => of(null),
  restart: () => of(null),
  dismissBlockFound: () => of(null)
};

const mockLocalStorageService = {
  getItem: () => null,
  setItem: () => {},
  getBool: () => false,
  setBool: () => {},
  getObject: () => null,
  setObject: () => {},
  getNumber: () => null,
  setNumber: () => {},
  removeItem: () => {}
};


describe('HomeComponent', () => {
  let component: HomeComponent;
  let fixture: ComponentFixture<HomeComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [
        HomeComponent,
        TooltipTextIconComponent,
        TooltipIconComponent,
        ConfettiComponent,
        SnowflakesComponent
      ],
      imports: [
        ReactiveFormsModule,
        FormsModule,
        NoopAnimationsModule,
        AppChartComponent,
        DropdownComponent,
        TooltipDirective,
        HashSuffixPipe,
        DiffSuffixPipe,
        DateAgoPipe,
        AddressPipe,
        SatsPipe,
        ByteSuffixPipe
      ],
      providers: [
        provideRouter([]),
        provideHttpClient(),
        provideToastr(),
        { provide: SystemApiService, useValue: mockSystemApiService },
        { provide: LiveDataService, useValue: mockLiveDataService },
        ThemeService,
        QuicklinkService,
        Title,
        LoadingService,
        ShareRejectionExplanationService,
        { provide: LocalStorageService, useValue: mockLocalStorageService },
        DashboardEditService,
        LayoutService
      ]
    });
    fixture = TestBed.createComponent(HomeComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  it('should render the dashboard widgets and dropdowns when info is loaded', () => {
    fixture.detectChanges();
    const element = fixture.nativeElement;
    // Verify that the dropdowns inside *ngIf are rendered
    expect(element.querySelector('app-dropdown')).toBeTruthy();
  });

  describe('stale data and visibility state', () => {
    it('should set stale data error when visible and last message is old', () => {
      spyOnProperty(document, 'visibilityState', 'get').and.returnValue('visible');

      component['lastMessageTime'] = Date.now() - 10000;
      component.systemInfoError$.next({ duration: 0, startTime: null });

      component['checkStaleData']();

      expect(component.systemInfoError$.value.duration).toBe(10);
    });

    it('should NOT set stale data error when hidden and last message is old', () => {
      spyOnProperty(document, 'visibilityState', 'get').and.returnValue('hidden');

      component['lastMessageTime'] = Date.now() - 10000;
      component.systemInfoError$.next({ duration: 0, startTime: null });

      component['checkStaleData']();

      expect(component.systemInfoError$.value.duration).toBe(0);
    });

    it('should reset lastMessageTime and clear stale error when transitioning to visible', () => {
      spyOnProperty(document, 'visibilityState', 'get').and.returnValue('visible');

      const initialTime = Date.now() - 10000;
      component['lastMessageTime'] = initialTime;
      component.systemInfoError$.next({ duration: 10, startTime: initialTime });

      component.onVisibilityChange();

      expect(component.systemInfoError$.value.duration).toBe(0);
      expect(component.systemInfoError$.value.startTime).toBeNull();
      expect(component['lastMessageTime']).toBeGreaterThan(initialTime);
    });
  });
});
