import 'chartjs-adapter-moment';
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { HomeComponent } from './home.component';
import { HttpErrorResponse, provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { ReactiveFormsModule, FormsModule } from '@angular/forms';
import { NoopAnimationsModule } from '@angular/platform-browser/animations';
import { Title } from '@angular/platform-browser';
import { provideRouter } from '@angular/router';
import { AppChartComponent } from 'src/app/components/chart/app-chart.component';
import { TooltipDirective } from 'src/app/directives/tooltip.directive';
import { DropdownComponent } from 'src/app/components/dropdown/dropdown.component';
import { ProgressbarComponent } from 'src/app/components/progressbar/progressbar.component';
import { BehaviorSubject, of, Subject } from 'rxjs';

import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { DiffSuffixPipe } from 'src/app/pipes/diff-suffix.pipe';
import { DateAgoPipe } from 'src/app/pipes/date-ago.pipe';
import { AddressPipe } from 'src/app/pipes/address.pipe';
import { SatsPipe } from 'src/app/pipes/sats.pipe';
import { ByteSuffixPipe } from 'src/app/pipes/byte-suffix.pipe';
import { HeatmapLightnessPipe } from 'src/app/pipes/heatmap-lightness.pipe';

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
  ASICModel: 'BM1370',
  apEnabled: 0,
  autofanspeed: 1,
  blockSignals: [],
  coinbaseValueUserSatoshis: 0,
  display: 'SSD1306',
  displayTimeout: 0,
  errorPercentage: 0,
  expectedHashrate: 500,
  frequency: 600,
  hostname: 'bitaxe',
  invertscreen: 0,
  isPSRAMAvailable: 1,
  manualFanSpeed: 80,
  minFanSpeed: 20,
  miningPaused: false,
  overclockEnabled: 0,
  overheat_mode: 0,
  partitions: [],
  pools: [],
  primaryPoolIndex: 0,
  resetReason: 'Power on',
  rotation: 0,
  runningPartition: 'ota_0',
  secondaryPoolIndex: 1,
  sharesRejectedReasons: [],
  smallCoreCount: 1,
  statsFrequency: 5,
  statsLimit: 720,
  stratumCert: '',
  stratumDecodeCoinbase: true,
  stratumExtranonceSubscribe: false,
  stratumSuggestedDifficulty: 1000,
  stratumTLS: false,
  stratumV2AuthorityPubkey: '',
  fallbackStratumCert: '',
  fallbackStratumDecodeCoinbase: true,
  fallbackStratumExtranonceSubscribe: false,
  fallbackStratumSuggestedDifficulty: 1000,
  fallbackStratumTLS: false,
  temptarget: 60,
  useCustomWWW: 0,
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
  isUsingFallbackStratum: 0,
  useFallbackStratum: 0
};

const mockSystemStatistics: ISystemStatistics = {
  labels: ['timestamp', 'hashrate', 'power'],
  statistics: [
    [Date.now() - 60000, 500, 15],
    [Date.now(), 500, 15]
  ],
  currentTimestamp: Date.now()
};

const mockLiveDataService = {
  info$: new BehaviorSubject<ISystemInfo>(mockSystemInfo),
  connected$: of(true)
};

const mockSystemApiService = {
  getStatistics: () => of(mockSystemStatistics),
  updateSystem: (_uri: string, _update: Pick<ISystemInfo, 'useFallbackStratum'>) => of(null),
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
    mockLiveDataService.info$ = new BehaviorSubject<ISystemInfo>(structuredClone(mockSystemInfo));
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
        ProgressbarComponent,
        TooltipDirective,
        HashSuffixPipe,
        DiffSuffixPipe,
        DateAgoPipe,
        AddressPipe,
        SatsPipe,
        ByteSuffixPipe,
        HeatmapLightnessPipe
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

  describe('pool selection', () => {
    function emitPoolInfo(changes: Partial<ISystemInfo>): void {
      mockLiveDataService.info$.next({ ...mockLiveDataService.info$.value, ...changes });
    }

    async function expectSelectedPool(label: 'Primary' | 'Fallback'): Promise<void> {
      fixture.detectChanges();
      await fixture.whenStable();
      fixture.detectChanges();
      const selectedLabel = fixture.nativeElement.querySelector('[gs-id="pool"] app-dropdown span');
      expect(selectedLabel.textContent.trim()).toBe(label);
    }

    async function selectPool(label: 'Primary' | 'Fallback'): Promise<void> {
      fixture.detectChanges();
      await fixture.whenStable();
      fixture.detectChanges();
      const dropdown: HTMLElement = fixture.nativeElement.querySelector('[gs-id="pool"] app-dropdown');
      dropdown.querySelector<HTMLElement>('[tabindex]')!.click();
      fixture.detectChanges();
      const option = Array.from(dropdown.querySelectorAll('li'))
        .find(item => item.textContent?.trim() === label)!;
      option.click();
      fixture.detectChanges();
      await fixture.whenStable();
      fixture.detectChanges();
    }

    it('should follow automatic failover and recovery while primary remains preferred', async () => {
      const updateSpy = spyOn(mockSystemApiService, 'updateSystem').and.callThrough();
      await expectSelectedPool('Primary');

      emitPoolInfo({ useFallbackStratum: 0, isUsingFallbackStratum: 1 });
      await expectSelectedPool('Fallback');
      expect(component.activePoolURL).toBe(mockSystemInfo.fallbackStratumURL);
      expect(component.activePoolUser).toBe(mockSystemInfo.fallbackStratumUser);

      emitPoolInfo({ useFallbackStratum: 0, isUsingFallbackStratum: 0 });
      await expectSelectedPool('Primary');
      expect(component.activePoolURL).toBe(mockSystemInfo.stratumURL);
      expect(component.activePoolUser).toBe(mockSystemInfo.stratumUser);
      expect(updateSpy).not.toHaveBeenCalled();
    });

    it('should show primary when fallback is preferred but primary is active', async () => {
      emitPoolInfo({ useFallbackStratum: 1, isUsingFallbackStratum: 0 });

      await expectSelectedPool('Primary');
      expect(component.activePoolURL).toBe(mockSystemInfo.stratumURL);
    });

    for (const target of ['Primary', 'Fallback'] as const) {
      it(`should preserve a pending manual switch to ${target} until the preference is acknowledged`, async () => {
        const targetFallback = Number(target === 'Fallback');
        const initialFallback = 1 - targetFallback;
        emitPoolInfo({ useFallbackStratum: initialFallback, isUsingFallbackStratum: initialFallback });
        const response = new Subject<null>();
        const updateSpy = spyOn(mockSystemApiService, 'updateSystem').and.returnValue(response);
        const restartSpy = spyOn(mockSystemApiService, 'restart').and.callThrough();

        await selectPool(target);
        expect(updateSpy.calls.mostRecent().args).toEqual(['', { useFallbackStratum: targetFallback }]);
        emitPoolInfo({ useFallbackStratum: initialFallback, isUsingFallbackStratum: initialFallback });
        await expectSelectedPool(target);

        response.next(null);
        response.complete();
        emitPoolInfo({ useFallbackStratum: targetFallback, isUsingFallbackStratum: targetFallback });
        await expectSelectedPool(target);

        // Once acknowledged, subsequent status changes must follow the active pool again.
        emitPoolInfo({ isUsingFallbackStratum: initialFallback });
        await expectSelectedPool(target === 'Fallback' ? 'Primary' : 'Fallback');
        expect(updateSpy).toHaveBeenCalledTimes(1);
        expect(restartSpy).not.toHaveBeenCalled();
      });
    }

    it('should show the active pool if the saved manual preference has not become active', async () => {
      spyOn(mockSystemApiService, 'updateSystem').and.returnValue(of(null));
      await selectPool('Fallback');

      emitPoolInfo({ useFallbackStratum: 1, isUsingFallbackStratum: 0 });

      await expectSelectedPool('Primary');
      expect(component.activePoolURL).toBe(mockSystemInfo.stratumURL);
    });

    it('should release a failed manual selection and follow subsequent pool status', async () => {
      const response = new Subject<null>();
      spyOn(mockSystemApiService, 'updateSystem').and.returnValue(response);
      await selectPool('Fallback');
      await expectSelectedPool('Fallback');

      response.error(new HttpErrorResponse({ status: 500, statusText: 'Pool update failed' }));
      emitPoolInfo({ useFallbackStratum: 0, isUsingFallbackStratum: 0 });
      await expectSelectedPool('Primary');

      emitPoolInfo({ isUsingFallbackStratum: 1 });
      await expectSelectedPool('Fallback');
    });
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

    it('should call loadPreviousData and not prematurely updateChart when awayTime exceeds threshold', () => {
      spyOnProperty(document, 'visibilityState', 'get').and.returnValue('visible');
      const loadSpy = spyOn<any>(component, 'loadPreviousData');
      const updateChartSpy = spyOn<any>(component, 'updateChart');

      component.dataLabel = [Date.now() - 30000];
      component['lastHiddenTime'] = Date.now() - 30000;
      component['lastStatsFrequency'] = 10;

      component.onVisibilityChange();

      expect(loadSpy).toHaveBeenCalledWith(false);
      expect(updateChartSpy).not.toHaveBeenCalled();
      expect(component['lastHiddenTime']).toBe(0);
    });

    it('should call updateChart immediately when awayTime is below threshold', () => {
      spyOnProperty(document, 'visibilityState', 'get').and.returnValue('visible');
      const loadSpy = spyOn<any>(component, 'loadPreviousData');
      const updateChartSpy = spyOn<any>(component, 'updateChart');

      component.dataLabel = [Date.now() - 1000];
      component['lastHiddenTime'] = Date.now() - 2000;
      component['lastStatsFrequency'] = 10;

      component.onVisibilityChange();

      expect(loadSpy).not.toHaveBeenCalled();
      expect(updateChartSpy).toHaveBeenCalledWith(undefined, true);
      expect(component['lastHiddenTime']).toBe(0);
    });

    it('should not update chartData reference when hidden in limitDataPoints', () => {
      spyOnProperty(document, 'visibilityState', 'get').and.returnValue('hidden');
      component['statsLimit'] = 2;
      component.dataLabel = [1000, 2000, 3000];
      component.hashrateData = [100, 100, 100];
      component.powerData = [10, 10, 10];
      component.chartDatasets = {};
      const originalChartData = { labels: [], datasets: [] };
      component.chartData = originalChartData;

      component.limitDataPoints(30);

      expect(component.chartData).toBe(originalChartData);
      expect(component.dataLabel.length).toBe(2);
    });

    it('should update chartData reference when visible in limitDataPoints', () => {
      spyOnProperty(document, 'visibilityState', 'get').and.returnValue('visible');
      component['statsLimit'] = 2;
      component.dataLabel = [1000, 2000, 3000];
      component.hashrateData = [100, 100, 100];
      component.powerData = [10, 10, 10];
      component.chartDatasets = {};
      const originalChartData = { labels: [], datasets: [] };
      component.chartData = originalChartData;

      component.limitDataPoints(30);

      expect(component.chartData).not.toBe(originalChartData);
      expect(component.dataLabel.length).toBe(2);
    });

    it('should clear flash timeouts on destroy', () => {
      component['shareAcceptedTimeout'] = setTimeout(() => {}, 10000) as any;
      component['shareRejectedTimeout'] = setTimeout(() => {}, 10000) as any;
      component['workReceivedTimeout'] = setTimeout(() => {}, 10000) as any;

      spyOn(window, 'clearTimeout').and.callThrough();

      component.ngOnDestroy();

      expect(clearTimeout).toHaveBeenCalledWith(component['shareAcceptedTimeout']);
      expect(clearTimeout).toHaveBeenCalledWith(component['shareRejectedTimeout']);
      expect(clearTimeout).toHaveBeenCalledWith(component['workReceivedTimeout']);
    });
  });
});
