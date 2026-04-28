import { Component, OnInit, ViewChild, Input, OnDestroy, ElementRef, HostListener, effect } from '@angular/core';
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
import { LayoutService } from 'src/app/layout/service/app.layout.service';
import { SystemInfo as ISystemInfo, SystemStatistics as ISystemStatistics } from 'src/app/generated/models';
import { Title } from '@angular/platform-browser';
import { UIChart } from 'primeng/chart';
import { SelectItem } from 'primeng/api';
import { eChartLabel } from 'src/models/enum/eChartLabel';
import { chartLabelValue } from 'src/models/enum/eChartLabel';
import { chartLabelKey } from 'src/models/enum/eChartLabel';
import { LocalStorageService } from 'src/app/local-storage.service';
import { GridStack, GridItemHTMLElement } from 'gridstack';
import { DashboardEditService, WidgetDef } from 'src/app/services/dashboard-edit.service';

type PoolLabel = 'Primary' | 'Fallback';
type MessageType =
  | 'SYSTEM_INFO_ERROR'
  | 'MINING_PAUSED'
  | 'DEVICE_OVERHEAT'
  | 'POWER_FAULT'
  | 'FREQUENCY_LOW'
  | 'FALLBACK_STRATUM'
  | 'VERSION_MISMATCH'
  | 'NOT_SOLO_MINING'
  | 'NO_MINING_REWARD'
  | 'HARDWARE_FAULT';

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
const DASHBOARD_LAYOUT_KEY = 'DASHBOARD_LAYOUT_V1';
const HIDDEN_WIDGETS_KEY = 'DASHBOARD_HIDDEN_WIDGETS';
const DEFAULT_CELL_HEIGHT = 40;

const WIDGET_DEFAULTS: WidgetDef[] = [
  { id: 'hashrate',    label: 'Hashrate',            x: 0, y: 0,   w: 3,  h: 5,  minW: 2, minH: 3 },
  { id: 'efficiency',  label: 'Efficiency',          x: 3, y: 0,   w: 3,  h: 5,  minW: 2, minH: 3 },
  { id: 'shares',      label: 'Shares',              x: 6, y: 0,   w: 3,  h: 5,  minW: 2, minH: 3 },
  { id: 'bestdiff',    label: 'Best Difficulty',     x: 9, y: 0,   w: 3,  h: 5,  minW: 2, minH: 3 },
  { id: 'chart',       label: 'Chart',               x: 0, y: 5,   w: 12, h: 0,  minW: 4, minH: 8 },
  { id: 'power',       label: 'Power',               x: 0, y: 5,   w: 4,  h: 7,  minW: 2, minH: 3 },
  { id: 'heat',        label: 'Heat',                x: 4, y: 5,   w: 4,  h: 7,  minW: 2, minH: 3 },
  { id: 'fan',         label: 'Fan',                 x: 8, y: 5,   w: 4,  h: 7,  minW: 2, minH: 3 },
  { id: 'pool',        label: 'Pool',                x: 0, y: 12,  w: 4,  h: 6,  minW: 2, minH: 3 },
  { id: 'blockheader', label: 'Block Header',        x: 4, y: 12,  w: 4,  h: 6,  minW: 2, minH: 3 },
  { id: 'registers',   label: 'Hashrate Registers',  x: 8, y: 12,  w: 4,  h: 6,  minW: 2, minH: 3 },
];

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

  private gridStackEl?: ElementRef<HTMLElement>;
  @ViewChild('gridStack', { static: false })
  set gridStackRef(el: ElementRef<HTMLElement>) {
    if (el && !this.grid) {
      this.gridStackEl = el;
      this.initGridStack();
    }
  }
  private grid!: GridStack;
  public editMode = false;
  public widgetDefs = WIDGET_DEFAULTS;
  public hiddenWidgets = new Set<string>();
  private stashedWidgets = new Map<string, HTMLElement>();

  private currentInterval: any = HomeComponent.ADAPTIVE_TICK_INTERVALS[0];
  private chartWidth: number = 800;

  private static readonly ADAPTIVE_TICK_INTERVALS = [
    { unit: 'second', step: 1, ms: 1000 },
    { unit: 'second', step: 2, ms: 2000 },
    { unit: 'second', step: 5, ms: 5000 },
    { unit: 'second', step: 10, ms: 10000 },
    { unit: 'second', step: 15, ms: 15000 },
    { unit: 'second', step: 30, ms: 30000 },
    { unit: 'minute', step: 1, ms: 60000 },
    { unit: 'minute', step: 2, ms: 120000 },
    { unit: 'minute', step: 5, ms: 300000 },
    { unit: 'minute', step: 10, ms: 600000 },
    { unit: 'minute', step: 15, ms: 900000 },
    { unit: 'minute', step: 30, ms: 1800000 },
    { unit: 'hour', step: 1, ms: 3600000 },
    { unit: 'hour', step: 2, ms: 7200000 },
    { unit: 'hour', step: 4, ms: 14400000 },
    { unit: 'hour', step: 6, ms: 21600000 },
    { unit: 'hour', step: 12, ms: 43200000 },
    { unit: 'day', step: 1, ms: 86400000 },
    { unit: 'day', step: 2, ms: 172800000 } // Max data retention is 1 month
  ];

  private pageDefaultTitle: string = '';
  private destroy$ = new Subject<void>();
  private infoSubscription?: Subscription;
  private liveDataStarted = false;
  private resizeTimer: any;
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
    private storageService: LocalStorageService,
    private dashboardEditService: DashboardEditService,
    public layoutService: LayoutService
  ) {
    this.initializeChart();

    effect(() => {
      // Refresh grid when wide view toggles
      if (this.layoutService.isWideView() !== undefined) {
        setTimeout(() => {
          this.grid?.compact();
          this.chart?.chart?.resize();
        }, 100);
      }
    });
  }

  ngOnInit(): void {
    this.dashboardEditService.widgetDefs = this.widgetDefs;
    this.dashboardEditService.isActive$.next(true);

    this.dashboardEditService.editMode$
      .pipe(takeUntil(this.destroy$))
      .subscribe(mode => {
        this.editMode = mode;
        if (this.grid) {
          this.grid.enableMove(mode);
          this.grid.enableResize(mode);
        }
      });

    this.dashboardEditService.resetRequested$
      .pipe(takeUntil(this.destroy$))
      .subscribe(() => this.resetLayout());

    this.dashboardEditService.toggleWidgetRequested$
      .pipe(takeUntil(this.destroy$))
      .subscribe(id => this.toggleWidgetVisibility(id));

    this.themeService.getThemeSettings()
      .pipe(takeUntil(this.destroy$))
      .subscribe(() => {
        this.updateChartColors();
      });

    this.pageDefaultTitle = this.titleService.getTitle();
    this.loadingService.loading$.next(true);

    let dataSources = this.storageService.getItem(HOME_CHART_DATA_SOURCES);
    let parsedConfig: any = { chartY1Data: chartLabelKey(eChartLabel.hashrate), chartY2Data: chartLabelKey(eChartLabel.asicTemp) };
    
    if (dataSources !== null) {
      try {
        const stored = JSON.parse(dataSources);
        if (stored.chartY1Data) parsedConfig.chartY1Data = stored.chartY1Data;
        if (stored.chartY2Data) parsedConfig.chartY2Data = stored.chartY2Data;
      } catch (e) { }
    }

    this.form = this.fb.group(parsedConfig);

    this.form.valueChanges.subscribe(() => {
      this.storageService.setItem(HOME_CHART_DATA_SOURCES, JSON.stringify(this.form.getRawValue()));
      this.infoSubscription?.unsubscribe();
      this.clearDataPoints();
      this.loadPreviousData();
    })

    this.loadPreviousData();
  }

  @HostListener('window:resize')
  onWindowResize() {
    clearTimeout(this.resizeTimer);
    this.resizeTimer = setTimeout(() => {
      this.chart?.chart?.resize();
      this.grid?.compact();
    }, 200);
  }

  ngOnDestroy() {
    clearTimeout(this.resizeTimer);
    this.dashboardEditService.isActive$.next(false);
    this.dashboardEditService.editMode$.next(false);
    this.destroy$.next();
    this.destroy$.complete();
    this.grid?.destroy(false);
  }

  private initGridStack(): void {
    // Load hidden widgets before grid init
    const savedHidden = this.storageService.getObject(HIDDEN_WIDGETS_KEY);
    if (Array.isArray(savedHidden)) {
      this.hiddenWidgets = new Set(savedHidden);
    }
    this.dashboardEditService.hiddenWidgets = new Set(this.hiddenWidgets);

    // Stash hidden items out of the container before gridstack initializes
    const container = this.gridStackEl!.nativeElement;
    this.hiddenWidgets.forEach(id => {
      const el = container.querySelector(`[gs-id="${id}"]`) as HTMLElement;
      if (el) {
        el.remove();
        this.stashedWidgets.set(id, el);
      }
    });

    this.grid = GridStack.init({
      column: 12,
      cellHeight: DEFAULT_CELL_HEIGHT,
      margin: 8,
      float: false,
      disableResize: true,
      disableDrag: true,
      animate: false,
      columnOpts: {
        breakpointForWindow: true,
        breakpoints: [
          { w: 768, c: 1 },
          { w: 1200, c: 6 },
        ],
        layout: 'list',
      },
    }, this.gridStackEl!.nativeElement);

    const savedLayout = this.storageService.getObject(DASHBOARD_LAYOUT_KEY);
    this.grid.load(savedLayout ?? this.getInitialLayout());

    setTimeout(() => this.chart?.chart?.resize(), 100);

    this.grid.on('change', () => {
      this.saveLayout();
    });

    this.grid.on('resizestop', (_event: Event, el: GridItemHTMLElement) => {
      if (el.gridstackNode?.id === 'chart') {
        const isMobile = window.innerWidth < 768;
        if (!isMobile && el.gridstackNode.h) {
           el.dataset['desktopH'] = String(el.gridstackNode.h);
        }
        setTimeout(() => this.chart?.chart?.resize(), 100);
      }
    });
  }

  private saveLayout(): void {
    const layout = this.grid.save(false);
    this.storageService.setObject(DASHBOARD_LAYOUT_KEY, layout as object);
  }

  public toggleEditMode(): void {
    this.dashboardEditService.toggleEditMode();
  }

  public resetLayout(): void {
    localStorage.removeItem(DASHBOARD_LAYOUT_KEY);
    localStorage.removeItem(HIDDEN_WIDGETS_KEY);
    this.grid.load(this.getInitialLayout());
  }

  private getInitialLayout(): WidgetDef[] {
    const chartDef = WIDGET_DEFAULTS.find(d => d.id === 'chart');
    if (!chartDef) return WIDGET_DEFAULTS;

    // The old layout set the chart height to 40vh. In gridstack, you need to set the height of 
    // the card, so there's 100px to compensate for the dropdowns and padding.
    const CHART_CHROME_PX = 100;
    const targetPx = (window.innerHeight * 0.40) + CHART_CHROME_PX;
    const chartH = Math.max(chartDef.minH ?? 8, Math.round(targetPx / DEFAULT_CELL_HEIGHT));

    return WIDGET_DEFAULTS.map(widget => {
      const w = { ...widget };
      if (w.id === chartDef.id) {
        w.h = chartH;
      } else if (w.y >= chartDef.y) {
        // Shift everything at or below the chart position
        w.y += chartH;
      }
      return w;
    });
  }

  public isWidgetVisible(id: string): boolean {
    return !this.hiddenWidgets.has(id);
  }

  public toggleWidgetVisibility(id: string): void {
    if (this.hiddenWidgets.has(id)) {
      // Show widget — restore stashed DOM element
      this.hiddenWidgets.delete(id);
      const stashed = this.stashedWidgets.get(id);
      if (stashed) {
        this.stashedWidgets.delete(id);
        this.grid.addWidget(stashed);
      }
    } else {
      // Hide widget — remove from grid and stash the DOM element
      const el = this.gridStackEl!.nativeElement.querySelector(`[gs-id="${id}"]`) as GridItemHTMLElement;
      if (el) {
        this.grid.removeWidget(el, false);
        el.remove();
        this.stashedWidgets.set(id, el);
      }
      this.hiddenWidgets.add(id);
    }
    this.saveHiddenWidgets();
    this.saveLayout();
  }

  private saveHiddenWidgets(): void {
    this.storageService.setObject(HIDDEN_WIDGETS_KEY, [...this.hiddenWidgets]);
    this.dashboardEditService.hiddenWidgets = new Set(this.hiddenWidgets);
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
      responsive: true,
      animation: false,
      maintainAspectRatio: false,
      onResize: (chart: any, size: { width: number; height: number }) => {
        this.chartWidth = size.width;
        const fontSize = Math.max(8, Math.min(12, Math.round(size.width / 50)));
        const tickFont = { size: fontSize };
        chart.options.scales.x.ticks.font = tickFont;
        chart.options.scales.y.ticks.font = tickFont;
        chart.options.scales.y2.ticks.font = tickFont;
        // Hide x-axis labels when chart is very short to reclaim space
        chart.options.scales.x.ticks.display = size.height > 100;
        this.updateAdaptiveTicks();
      },
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
            displayFormats: {
              millisecond: 'HH:mm:ss',
              second: 'HH:mm:ss',
              minute: 'HH:mm',
              hour: 'HH:mm',
            }
          },
          ticks: {
            color: textColorSecondary,
            autoSkip: false
          },
          afterBuildTicks: (axis: any) => {
            if (!this.currentInterval) return;

            const ticks = [];
            const start = new Date(axis.min);
            
            // Align start to the unit boundary (human readable)
            start.setMilliseconds(0);
            if (this.currentInterval.unit === 'second') {
              const s = start.getSeconds();
              start.setSeconds(s - (s % this.currentInterval.step));
            } else {
              start.setSeconds(0);
              if (this.currentInterval.unit === 'minute') {
                const m = start.getMinutes();
                start.setMinutes(m - (m % this.currentInterval.step));
              } else {
                start.setMinutes(0);
                if (this.currentInterval.unit === 'hour') {
                  const h = start.getHours();
                  start.setHours(h - (h % this.currentInterval.step));
                } else {
                  start.setHours(0);
                }
              }
            }

            let curr = start.getTime();
            // Start the first tick inside or exactly at the min boundary
            while (curr < axis.min) curr += this.currentInterval.ms;

            while (curr <= axis.max) {
              ticks.push({ value: curr });
              curr += this.currentInterval.ms;
            }
            axis.ticks = ticks;
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

          let statsFrequency = 0;
          if (stats.statistics.length >= 2 && idxTimestamp !== -1) {
            const totalDurationMs = stats.statistics[stats.statistics.length - 1][idxTimestamp] - stats.statistics[0][idxTimestamp];
            statsFrequency = Math.floor(totalDurationMs / (stats.statistics.length - 1) / 1000);
          }

          this.limitDataPoints(statsFrequency);
          this.updateAdaptiveTicks();
        });
        if (!this.liveDataStarted) {
          this.liveDataStarted = true;
          this.startGetLiveData();
        }
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

          this.limitDataPoints(info.statsFrequency);
          this.updateAdaptiveTicks();

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

  public dismissBlockFound(): void {
    this.systemService.dismissBlockFound()
      .pipe(
        this.loadingService.lockUIUntilComplete()
      )
      .subscribe({
        next: () => {
          this.toastr.success('Block found notification dismissed');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(`Error dismissing notification: ${err.message}`);
        }
      });
  }

  private setTitle(info: ISystemInfo, systemInfoError: ISystemInfoError) {
    const parts = [this.pageDefaultTitle];

    if (info.showNewBlock) {
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

  trackByIndex(index: number, _item: any) {
    return index;
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
    updateMessage(!!(info as any).miningPaused, 'MINING_PAUSED', 'warn', 'Mining is paused');
    updateMessage(info.overheat_mode === 1, 'DEVICE_OVERHEAT', 'error', 'Device has overheated - See settings');
    updateMessage(!!info.power_fault, 'POWER_FAULT', 'error', `${info.power_fault} Check your Power Supply.`);
    updateMessage(!!info.hardware_fault, 'HARDWARE_FAULT', 'error', `${info.hardware_fault}`);
    updateMessage(!info.frequency || info.frequency < 400, 'FREQUENCY_LOW', 'warn', 'Device frequency is set low - See settings');
    updateMessage(!!info.isUsingFallbackStratum, 'FALLBACK_STRATUM', 'warn', 'Using fallback pool - Share stats reset. Check Pool Settings and / or reboot Device.');
    updateMessage(info.version !== info.axeOSVersion, 'VERSION_MISMATCH', 'warn', `Firmware (${info.version}) and AxeOS (${info.axeOSVersion}) versions do not match. Please make sure to update both www.bin and esp-miner.bin.`);
    if (info.coinbaseOutputs && info.coinbaseOutputs?.length > 0) {
      let percentage = this.getPayoutPercentage(info);
      updateMessage(percentage > 0 && percentage < 95, 'NOT_SOLO_MINING', 'warn', `Your share of the mining reward is only ${percentage.toFixed(1)}%`);
      updateMessage(percentage === 0, 'NO_MINING_REWARD', 'warn', `You don't have a share in the mining reward`);
    }
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
    return info.hashrateMonitor?.asics?.length ?? 0;
  }

  public getAsicDomainsAmount(info: ISystemInfo): number {
    return info.hashrateMonitor?.asics?.[0]?.domains?.length ?? 0;
  }

  public getHeatmapColor(info: ISystemInfo, domainHashrate: number): string {
    const expectedHashrate = info.expectedHashrate || 1;
    const ratio = Math.max(0, Math.min(2, (domainHashrate / expectedHashrate) * this.getAsicsAmount(info)) * this.getAsicDomainsAmount(info));
    const deviation = isNaN(ratio) ? 1 : Math.abs(ratio - 1);  // 0 = perfect, 1 = 100% off
    const t = 1 - Math.pow(1 - deviation, 1.5); // Exponent controls graduality (lower = more gradual, 7 was very steep)
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

  public limitDataPoints(statsFrequency: number = 0) {
    const limit = 720;
    if (this.dataLabel.length <= limit) return;

    const statsFrequencyMs = (statsFrequency || 30) * 1000;
    const windowDurationMs = limit * statsFrequencyMs;

    while (this.dataLabel.length > limit) {
      const currentSpan = this.dataLabel[this.dataLabel.length - 1] - this.dataLabel[0];
      let indexToRemove = 0;

      // If the window is not full, thin based on significance (Triangle Area)
      if (currentSpan < windowDurationMs) {
        let minScore = Infinity;

        for (let i = 0; i < this.dataLabel.length - 1; i++) {
          const gapLeft = i > 0 ? this.dataLabel[i] - this.dataLabel[i - 1] : Infinity;
          const gapRight = this.dataLabel[i + 1] - this.dataLabel[i];

          if (gapLeft <= statsFrequencyMs || gapRight <= statsFrequencyMs) {
            const t1 = i > 0 ? this.dataLabel[i - 1] : this.dataLabel[i] - gapRight;
            const t2 = this.dataLabel[i];
            const t3 = this.dataLabel[i + 1];
            const v1 = i > 0 ? this.hashrateData[i - 1] : this.hashrateData[i];
            const v2 = this.hashrateData[i];
            const v3 = this.hashrateData[i + 1];

            const score = Math.abs(t1 * (v2 - v3) + t2 * (v3 - v1) + t3 * (v1 - v2));
            if (score < minScore) {
              minScore = score;
              indexToRemove = i;
            }
          }
        }
      }

      this.dataLabel.splice(indexToRemove, 1);
      this.hashrateData.splice(indexToRemove, 1);
      this.powerData.splice(indexToRemove, 1);
      this.chartY1Data.splice(indexToRemove, 1);
      this.chartY2Data.splice(indexToRemove, 1);
    }

    if (this.chartData) {
      this.chartData = { ...this.chartData };
    }
  }

  public updateAdaptiveTicks() {
    if (this.dataLabel.length < 2) return;

    const totalSpanMs = this.dataLabel[this.dataLabel.length - 1] - this.dataLabel[0];

    const maxTicks = Math.min(16, Math.max(3, Math.floor(this.chartWidth / 80)));

    this.currentInterval = HomeComponent.ADAPTIVE_TICK_INTERVALS.find(i => totalSpanMs / i.ms < maxTicks + 1) || 
                           HomeComponent.ADAPTIVE_TICK_INTERVALS[HomeComponent.ADAPTIVE_TICK_INTERVALS.length - 1];
    
    const xAxis = (this.chartOptions.scales as any).x;
    if (xAxis.time.unit !== this.currentInterval.unit || xAxis.time.stepSize !== this.currentInterval.step) {
      xAxis.time.unit = this.currentInterval.unit;
      xAxis.time.stepSize = this.currentInterval.step;
    }
  }

  public getSuggestedMaxForLabel(label: eChartLabel | undefined, info: ISystemInfo): number {
    switch (label) {
      case eChartLabel.hashrate:
      case eChartLabel.hashrate_1m:
      case eChartLabel.hashrate_10m:
      case eChartLabel.hashrate_1h:      return info.expectedHashrate;
      case eChartLabel.errorPercentage:  return 1;
      case eChartLabel.asicTemp:
      case eChartLabel.asicTemp2:        return this.maxTemp;
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
      case eChartLabel.asicTemp2:          return info.temp2;
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
      case eChartLabel.asicTemp2:
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
    if (value === undefined || value === null) return '';
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
      .filter(([key, ]) => key !== 'asicTemp2' || (info.temp2 && info.temp2 !== -1))
      .filter(([key, ]) => key !== 'fanRpm' || info.fanrpm)
      .filter(([key, ]) => key !== 'fan2Rpm' || info.fan2rpm)
      .map(([key, value]) => ({name: value, value: key}));
  }
}
