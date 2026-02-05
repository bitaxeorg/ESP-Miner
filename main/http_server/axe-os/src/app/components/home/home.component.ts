import { Component, OnInit, ViewChild, Input, OnDestroy } from '@angular/core';
import { interval, map, Observable, shareReplay, startWith, Subscription, switchMap, tap, first, Subject, takeUntil, BehaviorSubject, filter, catchError, of, combineLatest } from 'rxjs';
import { HttpErrorResponse } from '@angular/common/http';
import { FormBuilder, FormGroup } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { DateAgoPipe } from 'src/app/pipes/date-ago.pipe';
import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { ByteSuffixPipe } from 'src/app/pipes/byte-suffix.pipe';
import { DiffSuffixPipe } from 'src/app/pipes/diff-suffix.pipe';
import { QuicklinkService } from 'src/app/services/quicklink.service';
import { ShareRejectionExplanationService } from 'src/app/services/share-rejection-explanation.service';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemApiService } from 'src/app/services/system.service';
import { ThemeService } from 'src/app/services/theme.service';
import { SystemInfo as ISystemInfo, SystemStatistics as ISystemStatistics } from 'src/app/generated';
import { Title } from '@angular/platform-browser';
import { UIChart } from 'primeng/chart';
import { SelectItem } from 'primeng/api';
import { eChartLabel } from 'src/models/enum/eChartLabel';
import { chartLabelValue } from 'src/models/enum/eChartLabel';
import { chartLabelKey } from 'src/models/enum/eChartLabel';
import { LocalStorageService } from 'src/app/local-storage.service';

type PoolLabel = 'Primary' | 'Fallback';
type MessageType =
  | 'SYSTEM_INFO_ERROR'
  | 'DEVICE_OVERHEAT'
  | 'POWER_FAULT'
  | 'FREQUENCY_LOW'
  | 'FALLBACK_STRATUM'
  | 'VERSION_MISMATCH'
  | 'NOT_SOLO_MINING'
  | 'NO_MINING_REWARD';

interface ISystemMessage {
  type: MessageType;
  severity: 'error' | 'warn' | 'info';
  text: string;
}
interface ISystemInfoError {
  duration: number;
  startTime: number | null;
}

const HOME_CHART_DATA_SOURCES = 'HOME_CHART_DATA_SOURCES';

@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent implements OnInit, OnDestroy {
  public messages: ISystemMessage[] = [];

  public info$!: Observable<ISystemInfo>;
  public stats$!: Observable<ISystemStatistics>;
  public pools$!: Observable<SelectItem<PoolLabel>[]>;

  public chartOptions: any;
  public dataLabel: number[] = [];
  public hashrateData: number[] = [];
  public powerData: number[] = [];
  public chartY1Data: number[] = [];
  public chartY2Data: number[] = [];
  public chartData?: any;

  public maxPower: number = 0;
  public nominalVoltage: number = 0;
  public maxTemp: number = 75;
  public maxRpm: number = 7000;
  public maxFrequency: number = 800;

  public quickLink$!: Observable<string | undefined>;

  public activePoolURL!: string;
  public activePoolPort!: number;
  public activePoolUser!: string;
  public activePoolLabel!: PoolLabel;
  public responseTime!: number;

  public systemInfoError$ = new BehaviorSubject<ISystemInfoError>({
    duration: 0,
    startTime: null
  });

  public hashrateAverages: { label: string, key: 'hashRate_1m' | 'hashRate_10m' | 'hashRate_1h' }[] = [
    { label: '1m', key: 'hashRate_1m' },
    { label: '10m', key: 'hashRate_10m' },
    { label: '1h', key: 'hashRate_1h' }
  ];

  @ViewChild('chart')
  private chart?: UIChart

  private pageDefaultTitle: string = '';
  private destroy$ = new Subject<void>();
  private infoSubscription?: Subscription;
  public form!: FormGroup;

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemApiService,
    private themeService: ThemeService,
    private quickLinkService: QuicklinkService,
    private titleService: Title,
    private loadingService: LoadingService,
    private toastr: ToastrService,
    private shareRejectReasonsService: ShareRejectionExplanationService,
    private storageService: LocalStorageService
  ) {
    this.initializeChart();
  }

  ngOnInit(): void {
    this.themeService.getThemeSettings()
      .pipe(takeUntil(this.destroy$))
      .subscribe(() => {
        this.updateChartColors();
      });

    this.pageDefaultTitle = this.titleService.getTitle();
    this.loadingService.loading$.next(true);

    let dataSources = this.storageService.getItem(HOME_CHART_DATA_SOURCES);
    if (dataSources === null) {
      dataSources = `{"chartY1Data":"${chartLabelKey(eChartLabel.hashrate)}",`;
      dataSources += `"chartY2Data":"${chartLabelKey(eChartLabel.asicTemp)}"}`;
    }

    this.form = this.fb.group(JSON.parse(dataSources));

    this.form.valueChanges.subscribe(() => {
      this.updateSystem();
    })

    this.loadPreviousData();
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }

  private updateChartColors() {
    const documentStyle = getComputedStyle(document.documentElement);
    const textColorSecondary = documentStyle.getPropertyValue('--text-color-secondary');
    const surfaceBorder = documentStyle.getPropertyValue('--surface-border');
    const primaryColor = documentStyle.getPropertyValue('--primary-color');

    // Update chart colors
    if (this.chartData && this.chartData.datasets) {
      this.chartData.datasets[0].backgroundColor = primaryColor + '30';
      this.chartData.datasets[0].borderColor = primaryColor;
      this.chartData.datasets[1].backgroundColor = textColorSecondary;
      this.chartData.datasets[1].borderColor = textColorSecondary;
    }

    // Update chart options
    if (this.chartOptions) {
      this.chartOptions.scales.x.ticks.color = textColorSecondary;
      this.chartOptions.scales.x.grid.color = surfaceBorder;
      this.chartOptions.scales.y.ticks.color = primaryColor;
      this.chartOptions.scales.y.grid.color = surfaceBorder;
      this.chartOptions.scales.y2.ticks.color = textColorSecondary;
      this.chartOptions.scales.y2.grid.color = surfaceBorder;
    }

    // Force chart update
    this.chartData = { ...this.chartData };
  }

  public updateSystem() {
    const form = this.form.getRawValue();

    this.storageService.setItem(HOME_CHART_DATA_SOURCES, JSON.stringify(form));

    this.systemService.updateSystem(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.infoSubscription?.unsubscribe();
          this.clearDataPoints();
          this.loadPreviousData();
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save chart source. ${err.message}`);
        }
      });
  }

  private initializeChart() {
    const documentStyle = getComputedStyle(document.documentElement);
    const textColorSecondary = documentStyle.getPropertyValue('--text-color-secondary');
    const surfaceBorder = documentStyle.getPropertyValue('--surface-border');
    const primaryColor = documentStyle.getPropertyValue('--primary-color');

    this.chartData = {
      labels: [this.dataLabel],
      datasets: [
        {
          type: 'line',
          label: eChartLabel.hashrate,
          data: [this.chartY1Data],
          fill: true,
          backgroundColor: primaryColor + '30',
          borderColor: primaryColor,
          tension: 0,
          pointRadius: 2,
          pointHoverRadius: 5,
          borderWidth: 1,
          yAxisID: 'y',
          hidden: false
        },
        {
          type: 'line',
          label: eChartLabel.asicTemp,
          data: [this.chartY2Data],
          fill: false,
          backgroundColor: textColorSecondary,
          borderColor: textColorSecondary,
          tension: 0,
          pointRadius: 2,
          pointHoverRadius: 5,
          borderWidth: 1,
          yAxisID: 'y2',
          hidden: false
        }
      ]
    };

    this.chartOptions = {
      animation: false,
      maintainAspectRatio: false,
      plugins: {
        legend: {
          display: false
        },
        tooltip: {
          callbacks: {
            label: function (tooltipItem: any) {
              let label = tooltipItem.dataset.label || '';
              if (label) {
                return label += ': ' + HomeComponent.cbFormatValue(tooltipItem.raw, label);
              } else {
                return tooltipItem.raw;
              }
            }
          }
        },
      },
      interaction: {
        intersect: false,
        mode: 'index'
      },
      scales: {
        x: {
          type: 'time',
          time: {
            unit: 'hour', // Set the unit to 'minute'
          },
          ticks: {
            color: textColorSecondary
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false,
            display: true
          }
        },
        y: {
          type: 'linear',
          display: true,
          position: 'left',
          ticks: {
            color: primaryColor,
            callback: (value: number) => HomeComponent.cbFormatValue(value, this.chartData.datasets[0].label, {tickmark: true})
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false
          },
          suggestedMax: 0
        },
        y2: {
          type: 'linear',
          display: true,
          position: 'right',
          ticks: {
            color: textColorSecondary,
            callback: (value: number) => HomeComponent.cbFormatValue(value, this.chartData.datasets[1].label, {tickmark: true})
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },
          suggestedMax: 80
        }
      }
    };

    this.chartData.labels = this.dataLabel;
    this.chartData.datasets[0].data = this.chartY1Data;
    this.chartData.datasets[1].data = this.chartY2Data;
  }

  private loadPreviousData() {
    const chartY1DataLabel = this.form.get('chartY1Data')?.value;
    const chartY2DataLabel = this.form.get('chartY2Data')?.value;

    // load previous data
    this.stats$ = this.systemService.getStatistics(chartY1DataLabel, chartY2DataLabel)
      .pipe(shareReplay({ refCount: true, bufferSize: 1 }));

    this.stats$
      .pipe(takeUntil(this.destroy$))
      .subscribe(stats => {
        let idxHashrate = -1;
        let idxPower = -1;
        let idxChartY1Data = -1;
        let idxChartY2Data = -1;
        let idxTimestamp = -1;

        // map label to index
        for (let i = 0; i < stats.labels.length; i++) {
          if (stats.labels[i] === chartLabelKey(eChartLabel.hashrate)) { idxHashrate = i; }
          if (stats.labels[i] === chartLabelKey(eChartLabel.power))    { idxPower = i; }
          if (stats.labels[i] === chartY1DataLabel)                    { idxChartY1Data = i; }
          if (stats.labels[i] === chartY2DataLabel)                    { idxChartY2Data = i; }
          if (stats.labels[i] === 'timestamp')                         { idxTimestamp = i; }
        }

        stats.statistics.forEach((element: number[]) => {
          switch (chartLabelValue(chartY1DataLabel)) {
            case eChartLabel.asicVoltage:
            case eChartLabel.voltage:
            case eChartLabel.current:
              element[idxChartY1Data] = element[idxChartY1Data] / 1000;
              break;
            default:
              break;
          }
          switch (chartLabelValue(chartY2DataLabel)) {
            case eChartLabel.asicVoltage:
            case eChartLabel.voltage:
            case eChartLabel.current:
              element[idxChartY2Data] = element[idxChartY2Data] / 1000;
              break;
            default:
              break;
          }

          this.dataLabel.push(new Date().getTime() - stats.currentTimestamp + element[idxTimestamp]);
          this.hashrateData.push(element[idxHashrate]);
          this.powerData.push(element[idxPower]);
          if (-1 != idxChartY1Data) {
            this.chartY1Data.push(element[idxChartY1Data]);
          } else {
            this.chartY1Data.push(0.0);
          }
          if (-1 != idxChartY2Data) {
            this.chartY2Data.push(element[idxChartY2Data]);
          } else {
            this.chartY2Data.push(0.0);
          }

          this.limitDataPoints();
        }),
        this.startGetLiveData();
      });
  }

  private isHashrateAxis(label: eChartLabel | undefined) {
    return label == eChartLabel.hashrate || label == eChartLabel.hashrate_1m || label == eChartLabel.hashrate_10m || label == eChartLabel.hashrate_1h;
  }

  private startGetLiveData() {
    this.info$ = interval(5000).pipe(
      startWith(0),
      switchMap(() =>
        this.systemService.getInfo().pipe(
          tap(() => {
            const systemInfoError = this.systemInfoError$.value;
            if (!!systemInfoError.duration) {
              this.systemInfoError$.next({
                duration: 0,
                startTime: null
              });
            }
          }),
          catchError(() => {
            const now = Date.now();
            const systemInfoError = this.systemInfoError$.value;

            if (!systemInfoError.startTime) {
              this.systemInfoError$.next({
                duration: 0,
                startTime: now
              });
            } else {
              this.systemInfoError$.next({
                duration: (now - systemInfoError.startTime!) / 1000,
                startTime: systemInfoError.startTime
              });
            }
            return of(null);
          })
        )
      ),
      filter(info => info !== null),
      map(info => {
        info.voltage = info.voltage / 1000;
        info.current = info.current / 1000;
        info.coreVoltageActual = info.coreVoltageActual / 1000;
        info.coreVoltage = info.coreVoltage / 1000;
        return info;
      }),
      tap(info => {
        const chartY1DataLabel = chartLabelValue(this.form.get('chartY1Data')?.value);
        const chartY2DataLabel = chartLabelValue(this.form.get('chartY2Data')?.value);

        this.maxPower = Math.max(info.maxPower, info.power);
        this.nominalVoltage = info.nominalVoltage;
        this.maxTemp = Math.max(75, info.temp);
        this.maxRpm = Math.max(7000, info.fanrpm, info.fan2rpm);
        this.maxFrequency = Math.max(800, info.frequency);

        // Only collect and update chart data if there's no power fault
        if (!info.power_fault) {
          this.dataLabel.push(new Date().getTime());
          this.hashrateData.push(info.hashRate);
          this.powerData.push(info.power);
          this.chartY1Data.push(HomeComponent.getDataForLabel(chartY1DataLabel, info));
          this.chartY2Data.push(HomeComponent.getDataForLabel(chartY2DataLabel, info));

          this.limitDataPoints();

          this.chartData.datasets[0].label = chartY1DataLabel;
          this.chartData.datasets[1].label = chartY2DataLabel;

          this.chartData.datasets[0].hidden = (chartY1DataLabel === eChartLabel.none);
          this.chartData.datasets[1].hidden = (chartY2DataLabel === eChartLabel.none);

          // Align both axis if they're hashrates. TODO: for others, such as temperatures as well
          if (this.isHashrateAxis(chartY1DataLabel) && this.isHashrateAxis(chartY2DataLabel)) {
            this.chartOptions.scales.y.suggestedMin = this.chartOptions.scales.y2.suggestedMin = Math.min(...this.chartY1Data, ...this.chartY2Data);
            this.chartOptions.scales.y.suggestedMax = this.chartOptions.scales.y2.suggestedMax = Math.max(...this.chartY1Data, ...this.chartY2Data);
          } else {
            this.chartOptions.scales.y.suggestedMin = undefined;
            this.chartOptions.scales.y2.suggestedMin = undefined;
            this.chartOptions.scales.y.suggestedMax = this.getSuggestedMaxForLabel(chartY1DataLabel, info);
            this.chartOptions.scales.y2.suggestedMax = this.getSuggestedMaxForLabel(chartY2DataLabel, info);
          }

          this.chartOptions.scales.y.display = (chartY1DataLabel != eChartLabel.none);
          this.chartOptions.scales.y2.display = (chartY2DataLabel != eChartLabel.none);
        }

        this.chart?.refresh();

        const isFallbackPool = !!info.isUsingFallbackStratum;

        this.activePoolLabel = isFallbackPool ? 'Fallback' : 'Primary';
        this.activePoolURL = isFallbackPool ? info.fallbackStratumURL : info.stratumURL;
        this.activePoolUser = isFallbackPool ? info.fallbackStratumUser : info.stratumUser;
        this.activePoolPort = isFallbackPool ? info.fallbackStratumPort : info.stratumPort;
        this.responseTime = info.responseTime;
      }),
      map(info => {
        info.power = parseFloat(info.power.toFixed(1));
        info.voltage = parseFloat(info.voltage.toFixed(1));
        info.current = parseFloat(info.current.toFixed(1));
        info.coreVoltageActual = parseFloat(info.coreVoltageActual.toFixed(2));
        info.coreVoltage = parseFloat(info.coreVoltage.toFixed(2));
        info.temp = parseFloat(info.temp.toFixed(1));
        info.temp2 = parseFloat(info.temp2.toFixed(1));
        info.responseTime = parseFloat(info.responseTime.toFixed(1));

        return info;
      }),
      shareReplay({ refCount: true, bufferSize: 1 })
    );

    this.info$
      .pipe(first())
      .subscribe(() => {
        this.loadingService.loading$.next(false);
      });

    this.quickLink$ = this.info$.pipe(
      map(info => {
        const isFallbackPool = !!info.isUsingFallbackStratum;
        const url = isFallbackPool ? info.fallbackStratumURL : info.stratumURL;
        const user = isFallbackPool ? info.fallbackStratumUser : info.stratumUser;
        return this.quickLinkService.getQuickLink(url, user);
      })
    );

    this.pools$ = this.info$
      .pipe(map(info => {
        const result: SelectItem<PoolLabel>[] = [];
        if (info.stratumURL) {
          result.push({ label: 'Primary', value: 'Primary' });
        }
        if (info.fallbackStratumURL) {
          result.push({ label: 'Fallback', value: 'Fallback' });
        }
        return result;
      }));

    this.infoSubscription = combineLatest([this.info$, this.systemInfoError$])
      .pipe(takeUntil(this.destroy$))
      .subscribe(([info, systemInfoError]) => {
        this.handleSystemMessages(info, systemInfoError);
        this.setTitle(info, systemInfoError);
      });
  }

  onPoolChange(event: { originalEvent: Event; value: PoolLabel }) {
    const useFallbackStratum = Number(event.value === 'Fallback');

    this.systemService.updateSystem('', { useFallbackStratum })
      .pipe(
        this.loadingService.lockUIUntilComplete(),
        switchMap(() =>
          this.systemService.restart().pipe(
            this.loadingService.lockUIUntilComplete()
          )
        )
      )
      .subscribe({
        next: () => {
          this.toastr.success('Pool changed and device restarted');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(`Error during pool change or device restart: ${err.message}`);
        }
      });
  }

  private setTitle(info: ISystemInfo, systemInfoError: ISystemInfoError) {
    const parts = [this.pageDefaultTitle];

    if (info.blockFound) {
      parts.push('Block found 🎉');
    } else if (!!systemInfoError.duration) {
      parts.push('Unable to reach the device');
    } else {
      parts.push(
        info.hostname,
        (info.hashRate ? HashSuffixPipe.transform(info.hashRate) : ''),
        (info.temp ? `${info.temp}${info.temp2 > -1 ? `/${info.temp2}` : ''}${info.vrTemp ? `/${info.vrTemp}` : ''} °C` : ''),
        (!info.power_fault ? `${info.power} W` : ''),
        (info.bestDiff ? DiffSuffixPipe.transform(info.bestDiff) : ''),
      );
    }

    this.titleService.setTitle(parts.filter(Boolean).join(' • '));
  }

  private hexToRgb(hex: string): { r: number, g: number, b: number } {
    if (hex[0] === '#') hex = hex.slice(1);
    if (hex.length === 3) {
      hex = hex.split('').map((h: string) => h + h).join('');
    }

    const r = parseInt(hex.slice(0, 2), 16);
    const g = parseInt(hex.slice(2, 4), 16);
    const b = parseInt(hex.slice(4, 6), 16);

    return { r, g, b };
  }

  getRejectionExplanation(reason: string): string | null {
    return this.shareRejectReasonsService.getExplanation(reason);
  }

  getSortedRejectionReasons(info: ISystemInfo): ISystemInfo['sharesRejectedReasons'] {
    return [...(info.sharesRejectedReasons ?? [])].sort((a, b) => b.count - a.count);
  }

  trackByReason(_index: number, item: { message: string, count: number }) {
    return item.message; //Track only by message
  }

  public calculateAverage(data: number[]): number {
    if (data.length === 0) return 0;
    const sum = data.reduce((sum, value) => sum + value, 0);
    return sum / data.length;
  }

  public calculateEfficiencyAverage(hashrateData: number[], powerData: number[]): number {
    if (hashrateData.length === 0 || powerData.length === 0) return 0;

    // Calculate efficiency for each data point and average them
    const efficiencies = hashrateData.map((hashrate, index) => {
      const power = powerData[index] || 0;
      if (hashrate > 0) {
        return power / (hashrate / 1_000); // Convert to J/Th
      } else {
        return power; // in this case better than infinity or NaN
      }
    });

    return this.calculateAverage(efficiencies);
  }

  getPayoutPercentage(info: ISystemInfo) {
    if (info.coinbaseValueTotalSatoshis) {
      return (info.coinbaseValueUserSatoshis ?? 0) / info.coinbaseValueTotalSatoshis * 100;
    }
    return -1;
  }

  public handleSystemMessages(info: ISystemInfo, systemInfoError: ISystemInfoError) {
    const updateMessage = (
      condition: boolean,
      type: MessageType,
      severity: ISystemMessage['severity'],
      text: string
    ) => {
      const existingIndex = this.messages.findIndex(msg => msg.type === type);

      if (condition) {
        if (existingIndex === -1) {
          this.messages.push({ type, severity, text });
        } else {
          if (this.messages[existingIndex].text !== text) {
            this.messages.splice(existingIndex, 1, { type, severity, text });
          }
        }
      } else {
        if (existingIndex !== -1) {
          this.messages.splice(existingIndex, 1);
        }
      }
    };

    updateMessage(!!systemInfoError.duration, 'SYSTEM_INFO_ERROR', 'error', `Unable to reach the device for ${DateAgoPipe.transform(systemInfoError.duration, { strict: true })}`);
    updateMessage(info.overheat_mode === 1, 'DEVICE_OVERHEAT', 'error', 'Device has overheated - See settings');
    updateMessage(!!info.power_fault, 'POWER_FAULT', 'error', `${info.power_fault} Check your Power Supply.`);
    updateMessage(!info.frequency || info.frequency < 400, 'FREQUENCY_LOW', 'warn', 'Device frequency is set low - See settings');
    updateMessage(!!info.isUsingFallbackStratum, 'FALLBACK_STRATUM', 'warn', 'Using fallback pool - Share stats reset. Check Pool Settings and / or reboot Device.');
    updateMessage(info.version !== info.axeOSVersion, 'VERSION_MISMATCH', 'warn', `Firmware (${info.version}) and AxeOS (${info.axeOSVersion}) versions do not match. Please make sure to update both www.bin and esp-miner.bin.`);
    let percentage = this.getPayoutPercentage(info);
    updateMessage(percentage > 0 && percentage < 95, 'NOT_SOLO_MINING', 'warn', `Your share of the coinbase reward is only ${percentage.toFixed(1)}%`);
    updateMessage(percentage === 0, 'NO_MINING_REWARD', 'warn', `You don't have a share in the coinbase reward`);
  }

  private calculateEfficiency(info: ISystemInfo, key: 'hashRate' | 'expectedHashrate'): number {
    const hashrate = info[key];
    if (info.power_fault || hashrate <= 0) {
      return 0;
    }
    return info.power / (hashrate / 1_000);
  }

  public getHashrateAverage(): number {
    return this.calculateAverage(this.hashrateData);
  }

  public getEfficiency(info: ISystemInfo): number {
    return this.calculateEfficiency(info, 'hashRate');
  }

  public getEfficiencyAverage(): number {
    return this.calculateEfficiencyAverage(this.hashrateData, this.powerData);
  }

  public getExpectedEfficiency(info: ISystemInfo): number {
    return this.calculateEfficiency(info, 'expectedHashrate');
  }

  public getNetworkDifficultyPercentage(info: ISystemInfo): string {
    if (!info.networkDifficulty || info.networkDifficulty === 0) return '0';
    const percentage = (info.bestDiff / info.networkDifficulty) * 100;
    // Show 2 significant digits
    return percentage < 10 ? percentage.toPrecision(2) : percentage.toFixed(1);
  }

  public getShareRejectionPercentage(sharesRejectedReason: { count: number }, info: ISystemInfo): number {
    const totalShares = info.sharesAccepted + info.sharesRejected;
    if (totalShares <= 0) {
      return 0;
    }
    return (sharesRejectedReason.count / totalShares) * 100;
  }

  public getDomainErrorPercentage(info: ISystemInfo, asic: { error: number }): number {
    return asic.error ? (asic.error * 100 / info.expectedHashrate) : 0;
  }

  public getDomainErrorColor(info: ISystemInfo, asic: { error: number }): string {
    const percentage = this.getDomainErrorPercentage(info, asic);

    switch (true) {
      case (percentage < 1): return 'green';
      case (percentage >= 1 && percentage < 10): return 'orange';
      default: return 'red';
    }
  }

  public getAsicsAmount(info: ISystemInfo): number {
    return info.hashrateMonitor.asics.length;
  }

  public getAsicDomainsAmount(info: ISystemInfo): number {
    return info.hashrateMonitor.asics[0]?.domains?.length ?? 0;
  }

  public getHeatmapColor(info: ISystemInfo, domainHashrate: number): string {
    const ratio = Math.max(0, Math.min(2, (domainHashrate / info.expectedHashrate) * this.getAsicsAmount(info)) * this.getAsicDomainsAmount(info));
    const deviation = Math.abs(ratio - 1);  // 0 = perfect, 1 = 100% off
    const t = 1 - Math.pow(1 - deviation, 3);
    const target = ratio > 1 ? 255 : 0; // gradient from 0: black, 1: primary-color, 2: white

    const primaryColor = getComputedStyle(document.documentElement).getPropertyValue('--primary-color').trim();
    const { r, g, b } = this.hexToRgb(primaryColor);

    const finalR = (r * (1 - t) + target * t) | 0;
    const finalG = (g * (1 - t) + target * t) | 0;
    const finalB = (b * (1 - t) + target * t) | 0;

    return `rgb(${finalR}, ${finalG}, ${finalB})`;
  }

  public clearDataPoints() {
    this.dataLabel.length = 0;
    this.hashrateData.length = 0;
    this.powerData.length = 0;
    this.chartY1Data.length = 0;
    this.chartY2Data.length = 0;
  }

  public limitDataPoints() {
    if (this.dataLabel.length >= 720) {
      this.dataLabel.shift();
      this.hashrateData.shift();
      this.powerData.shift();
      this.chartY1Data.shift();
      this.chartY2Data.shift();
    }
  }

  public getSuggestedMaxForLabel(label: eChartLabel | undefined, info: ISystemInfo): number {
    switch (label) {
      case eChartLabel.hashrate:
      case eChartLabel.hashrate_1m:
      case eChartLabel.hashrate_10m:
      case eChartLabel.hashrate_1h:      return info.expectedHashrate;
      case eChartLabel.errorPercentage:  return 1;
      case eChartLabel.asicTemp:         return this.maxTemp;
      case eChartLabel.vrTemp:           return this.maxTemp + 25;
      case eChartLabel.asicVoltage:      return info.coreVoltage;
      case eChartLabel.voltage:          return info.nominalVoltage + .5;
      case eChartLabel.power:            return this.maxPower;
      case eChartLabel.current:          return this.maxPower / info.coreVoltage;
      case eChartLabel.fanSpeed:         return 100;
      case eChartLabel.fanRpm:           return 7000;
      case eChartLabel.fan2Rpm:          return 7000;
      case eChartLabel.responseTime:     return 50;
      default:                           return 0;
    }
  }

  static getDataForLabel(label: eChartLabel | undefined, info: ISystemInfo): number {
    switch (label) {
      case eChartLabel.hashrate:           return info.hashRate;
      case eChartLabel.hashrate_1m:        return info.hashRate_1m;
      case eChartLabel.hashrate_10m:       return info.hashRate_10m;
      case eChartLabel.hashrate_1h:        return info.hashRate_1h;
      case eChartLabel.errorPercentage:    return info.errorPercentage;
      case eChartLabel.asicTemp:           return info.temp;
      case eChartLabel.vrTemp:             return info.vrTemp;
      case eChartLabel.asicVoltage:        return info.coreVoltageActual;
      case eChartLabel.voltage:            return info.voltage;
      case eChartLabel.power:              return info.power;
      case eChartLabel.current:            return info.current;
      case eChartLabel.fanSpeed:           return info.fanspeed;
      case eChartLabel.fanRpm:             return info.fanrpm;
      case eChartLabel.fan2Rpm:            return info.fan2rpm;
      case eChartLabel.wifiRssi:           return info.wifiRSSI;
      case eChartLabel.freeHeap:           return info.freeHeap;
      case eChartLabel.responseTime:       return info.responseTime;
      default:                             return 0.0;
    }
  }

  static getSettingsForLabel(label: eChartLabel): {suffix: string; precision: number} {
    switch (label) {
      case eChartLabel.errorPercentage:  return {suffix: ' %', precision: 2};
      case eChartLabel.asicTemp:
      case eChartLabel.vrTemp:           return {suffix: ' °C', precision: 1};
      case eChartLabel.asicVoltage:
      case eChartLabel.voltage:          return {suffix: ' V', precision: 1};
      case eChartLabel.power:            return {suffix: ' W', precision: 1};
      case eChartLabel.current:          return {suffix: ' A', precision: 1};
      case eChartLabel.fanSpeed:         return {suffix: ' %', precision: 1};
      case eChartLabel.fanRpm:
      case eChartLabel.fan2Rpm:          return {suffix: ' rpm', precision: 0};
      case eChartLabel.wifiRssi:         return {suffix: ' dBm', precision: 0};
      case eChartLabel.responseTime:     return {suffix: ' ms', precision: 1};
      default:                           return {suffix: '', precision: 0};
    }
  }

  static cbFormatValue(value: number, datasetLabel: eChartLabel, args?: any): string {
    switch (datasetLabel) {
      case eChartLabel.hashrate:
      case eChartLabel.hashrate_1m:
      case eChartLabel.hashrate_10m:
      case eChartLabel.hashrate_1h:
        return HashSuffixPipe.transform(value, args);
      case eChartLabel.freeHeap:
        return ByteSuffixPipe.transform(value, args);
      default:
        const settings = HomeComponent.getSettingsForLabel(datasetLabel);
        return value.toLocaleString(undefined, { useGrouping: false, maximumFractionDigits: args?.tickmark ? undefined : settings.precision }) + settings.suffix;
    }
  }

  getAddressPart(user: string): string {
    const dotIndex = user.lastIndexOf('.');
    return dotIndex !== -1 ? user.substring(0, dotIndex) : user;
  }

  getSuffixPart(user: string): string {
    const dotIndex = user.lastIndexOf('.');
    return dotIndex !== -1 ? '.' + user.substring(dotIndex + 1) : '';
  }

  dataSourceLabels(info: ISystemInfo) {
    return Object.entries(eChartLabel)
      .filter(([key, ]) => key !== 'vrTemp' || info.vrTemp)
      .map(([key, value]) => ({name: value, value: key}));
  }
}
