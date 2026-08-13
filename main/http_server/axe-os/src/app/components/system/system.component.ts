import { Component, OnInit, OnDestroy } from '@angular/core';
import { Observable, Subject, combineLatest, shareReplay, first, takeUntil, map } from 'rxjs';
import { HttpErrorResponse } from '@angular/common/http';
import { getHttpErrorMessage } from 'src/app/utils/error-handler';
import { ToastrService } from 'ngx-toastr';
import { SystemApiService } from 'src/app/services/system.service';
import { LiveDataService } from 'src/app/services/live-data.service';
import { LoadingService } from 'src/app/services/loading.service';
import { DateAgoPipe } from 'src/app/pipes/date-ago.pipe';
import { ByteSuffixPipe } from 'src/app/pipes/byte-suffix.pipe';
import { SystemInfo as ISystemInfo, SystemAsic as ISystemASIC, GenericResponse, } from 'src/app/generated/models';
import { formatNumber } from '@angular/common';
import { I18nService } from 'src/app/i18n/i18n.service';

type TableRow = {
  label: string;
  value: string;
  class?: string;
  valueClass?: string;
  color?: string;
  isSensitiveData?: boolean;
  tooltip?: string;
}

type CombinedData = {
  info: ISystemInfo,
  asic: ISystemASIC
};

@Component({
    selector: 'app-system',
    templateUrl: './system.component.html',
    standalone: false
})
export class SystemComponent implements OnInit, OnDestroy {
  public systemRows$: Observable<TableRow[]>;
  public isConnected$: Observable<boolean>;

  private destroy$ = new Subject<void>();

  constructor(
    private systemService: SystemApiService,
    private liveDataService: LiveDataService,
    private loadingService: LoadingService,
    private toastr: ToastrService,
    private i18n: I18nService,
  ) {
    this.isConnected$ = this.liveDataService.connected$;
    
    const info$ = this.liveDataService.info$;
    const asic$ = this.systemService.getAsicSettings().pipe(
      shareReplay({ refCount: true, bufferSize: 1 })
    );

    const combinedData$ = combineLatest([info$, asic$]).pipe(
      map(([info, asic]) => ({ info, asic }))
    );

    this.systemRows$ = combinedData$.pipe(
      map(data => this.getSystemRows(data))
    );
  }

  ngOnInit() {
    this.systemRows$
      .pipe(first(), this.loadingService.lockUIUntilComplete(), takeUntil(this.destroy$))
      .subscribe();
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }

  trackByRowLabel(index: number, row: TableRow): string {
    return row.label;
  }

  getWifiRssiColor(rssi: number): string {
    if (rssi > -50) return 'text-green-500';
    if (rssi <= -50 && rssi > -60) return 'text-blue-500';
    if (rssi <= -60 && rssi > -70) return 'text-orange-500';

    return 'text-red-500';
  }

  getWifiRssiTooltip(rssi: number): string {
    if (rssi > -50) return this.i18n.t('device.system.wifi_rssi.excellent');
    if (rssi <= -50 && rssi > -60) return this.i18n.t('device.system.wifi_rssi.good');
    if (rssi <= -60 && rssi > -70) return this.i18n.t('device.system.wifi_rssi.fair');

    return this.i18n.t('device.system.wifi_rssi.weak');
  }

  getSystemRows(data: CombinedData): TableRow[] {
    const rows: TableRow[] = [
      { label: this.i18n.t('device.system.device_model'), value: data.asic.deviceModel || this.i18n.t('device.system.other'), color: data.asic.swarmColor || 'gray' },
      { label: this.i18n.t('device.system.board_version'), value: data.info.boardVersion },
      { label: this.i18n.t('device.system.asic_type'), value: (data.asic.asicCount > 1 ? data.asic.asicCount + 'x ' : ' ') + data.asic.ASICModel, class: 'pb-6' },
      { label: this.i18n.t('device.system.uptime'), value: DateAgoPipe.transform(data.info.uptimeSeconds) },
      { label: this.i18n.t('device.system.total_uptime'), value: DateAgoPipe.transform(data.info.totalUptimeSeconds || 0) },
      { label: this.i18n.t('device.system.total_log2_work'), value: (data.info.totalLog2Work || 0).toFixed(6) },
      { label: this.i18n.t('device.system.total_hashes'), value: formatNumber(data.info.totalHashes || 0, 'en-us') },
      { label: this.i18n.t('device.system.reset_reason'), value: data.info.resetReason, class: 'pb-6' },
      { label: this.i18n.t('device.system.wifi_ssid'), value: data.info.ssid, isSensitiveData: true },
      { label: this.i18n.t('device.system.wifi_status'), value: data.info.wifiStatus },
      { label: this.i18n.t('device.system.wifi_rssi.label'), value: data.info.wifiRSSI + ' dBm', valueClass: this.getWifiRssiColor(data.info.wifiRSSI), tooltip: this.getWifiRssiTooltip(data.info.wifiRSSI) },
      { label: this.i18n.t('device.system.wifi_ipv4'), value: data.info.ipv4},
      { label: this.i18n.t('device.system.wifi_ipv6'), value: data.info.ipv6, class: 'pb-6', isSensitiveData: true},
      { label: this.i18n.t('device.system.mac_address'), value: data.info.macAddr, class: 'pb-6', isSensitiveData: true },
      { label: this.i18n.t('device.system.cpu_usage'), value: data.info.cpuUsage.toFixed(1) + '%'},
      { label: this.i18n.t('device.system.free_heap'), value: ByteSuffixPipe.transform(data.info.freeHeap)},
      { label: this.i18n.t('device.system.free_heap_internal'), value: ByteSuffixPipe.transform(data.info.freeHeapInternal)},
      { label: this.i18n.t('device.system.free_heap_spiram'), value: ByteSuffixPipe.transform(data.info.freeHeapSpiram) },
      { label: this.i18n.t('device.system.min_free_heap'), value: ByteSuffixPipe.transform(data.info.minFreeHeap)},
      { label: this.i18n.t('device.system.max_alloc_heap'), value: ByteSuffixPipe.transform(data.info.maxAllocHeap), class: 'pb-6' },
      { label: this.i18n.t('device.system.firmware_version'), value: data.info.version },
    ];

    if (data.info.useCustomWWW === 1) {
      rows.push({ label: this.i18n.t('device.system.axeos_version'), value: data.info.axeOSVersion });
    }

    rows.push({ label: this.i18n.t('device.system.idf_version'), value: data.info.idfVersion });

    return rows;
  }

  identifyDevice(): void {
    this.systemService.identify()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: (result) => {
          this.toastr.success((result as GenericResponse).message);
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(this.i18n.t('errors.identify_failed', { error: getHttpErrorMessage(err) }));
        }
      });
  }
}
