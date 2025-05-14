import { Component, OnInit, ViewChild, OnDestroy } from '@angular/core';
import { interval, map, Observable, shareReplay, startWith, switchMap, tap, first, Subject, takeUntil } from 'rxjs';
import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { ByteSuffixPipe } from 'src/app/pipes/byte-suffix.pipe';
import { QuicklinkService } from 'src/app/services/quicklink.service';
import { ShareRejectionExplanationService } from 'src/app/services/share-rejection-explanation.service';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';
import { ThemeService } from 'src/app/services/theme.service';
import { ISystemInfo } from 'src/models/ISystemInfo';
import { ISystemStatistics } from 'src/models/ISystemStatistics';
import { Title } from '@angular/platform-browser';
import { UIChart } from 'primeng/chart';
import { eChartLabel } from 'src/models/enum/eChartLabel';

@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent implements OnInit, OnDestroy {

  public info$!: Observable<ISystemInfo>;
  public stats$!: Observable<ISystemStatistics>;

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
  public maxFrequency: number = 800;

  public quickLink$!: Observable<string | undefined>;

  public activePoolURL!: string;
  public activePoolPort!: number;
  public activePoolUser!: string;
  public activePoolLabel!: 'Primary' | 'Fallback';
  public responseTime!: number;

  @ViewChild('chart')
  private chart?: UIChart

  private pageDefaultTitle: string = '';
  private destroy$ = new Subject<void>();

  constructor(
    private systemService: SystemService,
    private themeService: ThemeService,
    private quickLinkService: QuicklinkService,
    private titleService: Title,
    private loadingService: LoadingService,
    private shareRejectReasonsService: ShareRejectionExplanationService
  ) {
    this.initializeChart();

    // Subscribe to theme changes
    this.themeService.getThemeSettings()
      .pipe(takeUntil(this.destroy$))
      .subscribe(() => {
        this.updateChartColors();
      });
  }

  ngOnInit() {
    this.pageDefaultTitle = this.titleService.getTitle();
    this.loadingService.loading$.next(true);
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }

  private updateChartColors() {
    const documentStyle = getComputedStyle(document.documentElement);
    const textColor = documentStyle.getPropertyValue('--text-color');
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
      this.chartOptions.plugins.legend.labels.color = textColor;
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

  private initializeChart() {
    const documentStyle = getComputedStyle(document.documentElement);
    const textColor = documentStyle.getPropertyValue('--text-color');
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
        }
      ]
    };

    this.chartOptions = {
      animation: false,
      maintainAspectRatio: false,
      plugins: {
        legend: {
          labels: {
            color: textColor
          }
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
            callback: (value: number) => HomeComponent.cbFormatValue(value, this.chartData.datasets[0].label)
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
            callback: (value: number) => HomeComponent.cbFormatValue(value, this.chartData.datasets[1].label)
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

    // load previous data
    this.stats$ = this.systemService.getStatistics()
      .pipe(shareReplay({ refCount: true, bufferSize: 1 }));

    this.stats$
      .pipe(takeUntil(this.destroy$))
      .subscribe(stats => {
        const idxTimestamp = 0;
        const idxHashrate = 1;
        const idxPower = 2;
        let idxChartY1Data = 3;
        let idxChartY2Data = 4;

        if (stats.chartY1Data === eChartLabel.hashrate) {
          idxChartY1Data = 1;
        } else if (stats.chartY1Data === eChartLabel.power) {
          idxChartY1Data = 2;
        }

        if (stats.chartY2Data === eChartLabel.hashrate) {
          idxChartY2Data = 1;
        } else if (stats.chartY2Data === eChartLabel.power) {
          idxChartY2Data = 2;
        } else if (idxChartY1Data < 3) { // no additional data for Y1
          idxChartY2Data = 3;
        }

        stats.statistics.forEach(element => {
          element[idxHashrate] = element[idxHashrate] * 1000000000; // convert to H/s

          this.dataLabel.push(new Date().getTime() - stats.currentTimestamp + element[idxTimestamp]);
          this.hashrateData.push(element[idxHashrate]);
          this.powerData.push(element[idxPower]);
          if (idxChartY1Data < element.length) {
            this.chartY1Data.push(element[idxChartY1Data]);
          } else {
            this.chartY1Data.push(0.0);
          }
          if (idxChartY2Data < element.length) {
            this.chartY2Data.push(element[idxChartY2Data]);
          } else {
            this.chartY2Data.push(0.0);
          }

          if (this.dataLabel.length >= 720) {
            this.dataLabel.shift();
            this.hashrateData.shift();
            this.powerData.shift();
            this.chartY1Data.shift();
            this.chartY2Data.shift();
          }
        }),
        this.startGetLiveData();
      });
  }

  private startGetLiveData()
  {
     // live data
    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      map(info => {
        info.hashRate = info.hashRate * 1000000000; // convert to H/s
        info.voltage = info.voltage / 1000;
        info.current = info.current / 1000;
        info.coreVoltageActual = info.coreVoltageActual / 1000;
        info.coreVoltage = info.coreVoltage / 1000;
        return info;
      }),
      tap(info => {
        // Only collect and update chart data if there's no power fault
        if (!info.power_fault) {
          this.dataLabel.push(new Date().getTime());
          this.hashrateData.push(info.hashRate);
          this.powerData.push(info.power);
          this.chartY1Data.push(HomeComponent.getDataForLabel(info.chartY1Data, info));
          this.chartY2Data.push(HomeComponent.getDataForLabel(info.chartY2Data, info));

          if ((this.dataLabel.length) >= 720) {
            this.dataLabel.shift();
            this.hashrateData.shift();
            this.powerData.shift();
            this.chartY1Data.shift();
            this.chartY2Data.shift();
          }

          this.chartData.datasets[0].label = info.chartY1Data;
          this.chartData.datasets[1].label = info.chartY2Data;
          this.chartOptions.scales.y.suggestedMax = HomeComponent.getSettingsForLabel(info.chartY1Data).suggestedMax;
          this.chartOptions.scales.y2.suggestedMax = HomeComponent.getSettingsForLabel(info.chartY2Data).suggestedMax;
        }

        this.chart?.refresh();
        this.maxPower = Math.max(info.maxPower, info.power);
        this.nominalVoltage = info.nominalVoltage;
        this.maxTemp = Math.max(75, info.temp);
        this.maxFrequency = Math.max(800, info.frequency);

        const isFallback = info.isUsingFallbackStratum;

        this.activePoolLabel = isFallback ? 'Fallback' : 'Primary';
        this.activePoolURL = isFallback ? info.fallbackStratumURL : info.stratumURL;
        this.activePoolUser = isFallback ? info.fallbackStratumUser : info.stratumUser;
        this.activePoolPort = isFallback ? info.fallbackStratumPort : info.stratumPort;
        this.responseTime = info.responseTime;
      }),
      map(info => {
        info.power = parseFloat(info.power.toFixed(1))
        info.voltage = parseFloat(info.voltage.toFixed(1));
        info.current = parseFloat(info.current.toFixed(1));
        info.coreVoltageActual = parseFloat(info.coreVoltageActual.toFixed(2));
        info.coreVoltage = parseFloat(info.coreVoltage.toFixed(2));
        info.temp = parseFloat(info.temp.toFixed(1));
        info.temp2 = parseFloat(info.temp2.toFixed(1));

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
        const url = info.isUsingFallbackStratum ? info.fallbackStratumURL : info.stratumURL;
        const user = info.isUsingFallbackStratum ? info.fallbackStratumUser : info.stratumUser;
        return this.quickLinkService.getQuickLink(url, user);
      })
    );

    this.info$
      .pipe(takeUntil(this.destroy$))
      .subscribe(info => {
        this.titleService.setTitle(
          [
            this.pageDefaultTitle,
            info.hostname,
            (info.hashRate ? HashSuffixPipe.transform(info.hashRate * 1000000000) : false),
            (info.temp ? `${info.temp}${info.temp2 > -1 ? `/${info.temp2}` : ''}${info.vrTemp ? `/${info.vrTemp}` : ''} °C` : false),
            (!info.power_fault ? `${info.power} W` : false),
            (info.bestDiff ? info.bestDiff : false),
          ].filter(Boolean).join(' • ')
        );
      });
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
        return power / (hashrate / 1000000000000); // Convert to J/Th
      } else {
        return power; // in this case better than infinity or NaN
      }
    });

    return this.calculateAverage(efficiencies);
  }

  static getDataForLabel(label: eChartLabel, info: ISystemInfo): number {
    switch (label) {
      case eChartLabel.hashrate:    return info.hashRate;
      case eChartLabel.asicTemp:    return info.temp;
      case eChartLabel.vrTemp:      return info.vrTemp;
      case eChartLabel.asicVoltage: return info.coreVoltageActual;
      case eChartLabel.voltage:     return info.voltage;
      case eChartLabel.power:       return info.power;
      case eChartLabel.current:     return info.current;
      case eChartLabel.fanSpeed:    return info.fanspeed;
      case eChartLabel.fanRpm:      return info.fanrpm;
      case eChartLabel.wifiRssi:    return info.wifiRSSI;
      case eChartLabel.freeHeap:    return info.freeHeap;
      default: return 0.0;
    }
  }

  static getSettingsForLabel(label: eChartLabel): {suffix: string; precision: number; suggestedMax: number} {
    switch (label) {
      case eChartLabel.hashrate:    return {suffix: ' H/s', precision: 0, suggestedMax: 0};
      case eChartLabel.asicTemp:    return {suffix: ' °C', precision: 1, suggestedMax: 80};
      case eChartLabel.vrTemp:      return {suffix: ' °C', precision: 1, suggestedMax: 110};
      case eChartLabel.asicVoltage: return {suffix: ' V', precision: 2, suggestedMax: 1.5};
      case eChartLabel.voltage:     return {suffix: ' V', precision: 1, suggestedMax: 5.2};
      case eChartLabel.power:       return {suffix: ' W', precision: 1, suggestedMax: 40};
      case eChartLabel.current:     return {suffix: ' A', precision: 1, suggestedMax: 20};
      case eChartLabel.fanSpeed:    return {suffix: ' %', precision: 0, suggestedMax: 100};
      case eChartLabel.fanRpm:      return {suffix: ' rpm', precision: 0, suggestedMax: 7000};
      case eChartLabel.wifiRssi:    return {suffix: ' dBm', precision: 0, suggestedMax: 0};
      case eChartLabel.freeHeap:    return {suffix: ' B', precision: 0, suggestedMax: 0};
      default: return {suffix: '', precision: 0, suggestedMax: 0};
    }
  }

  static cbFormatValue(value: number, datasetLabel: eChartLabel): string {
    switch (datasetLabel) {
      case eChartLabel.hashrate:    return HashSuffixPipe.transform(value);
      case eChartLabel.freeHeap:    return ByteSuffixPipe.transform(value);
      default:
        const settings = HomeComponent.getSettingsForLabel(datasetLabel);
        return value.toFixed(settings.precision) + settings.suffix;
    }
  }
}
