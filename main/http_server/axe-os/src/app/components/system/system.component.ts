import { Component, OnInit, OnDestroy } from '@angular/core';
import { Observable, Subject, combineLatest, switchMap, shareReplay, first, takeUntil, map, timer } from 'rxjs';
import { HttpErrorResponse } from '@angular/common/http';
import { ToastrService } from 'ngx-toastr';
import { SystemApiService } from 'src/app/services/system.service';
import { LoadingService } from 'src/app/services/loading.service';
import { DateAgoPipe } from 'src/app/pipes/date-ago.pipe';
import { ByteSuffixPipe } from 'src/app/pipes/byte-suffix.pipe';
import { SystemInfo as ISystemInfo, SystemASIC as ISystemASIC, GenericResponse, } from 'src/app/generated';
import { I18nService } from 'src/app/i18n/i18n.service';

type TableRow = {
  label: string;
  value: string;
  class?: string;
  valueClass?: string;
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
})
export class SystemComponent implements OnInit, OnDestroy {
  public info$: Observable<ISystemInfo>;
  public asic$: Observable<ISystemASIC>;
  public combinedData$: Observable<{ info: ISystemInfo, asic: ISystemASIC }>

  private destroy$ = new Subject<void>();

  constructor(
    private systemService: SystemApiService,
    private loadingService: LoadingService,
    private toastr: ToastrService,
    private i18n: I18nService,
  ) {
    this.info$ = timer(0, 5000).pipe(
      switchMap(() => this.systemService.getInfo()),
      shareReplay({ refCount: true, bufferSize: 1 })
    );

    this.asic$ = this.systemService.getAsicSettings().pipe(
      shareReplay({ refCount: true, bufferSize: 1 })
    );

    this.combinedData$ = combineLatest([this.info$, this.asic$]).pipe(
      map(([info, asic]) => ({ info, asic }))
    );
  }

  ngOnInit() {
    this.loadingService.loading$.next(true);

    this.combinedData$
      .pipe(first(), takeUntil(this.destroy$))
      .subscribe({
        next: () => this.loadingService.loading$.next(false)
      });
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
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
    return [
      { label: this.i18n.t('device.system.device_model'), value: data.asic.deviceModel || this.i18n.t('device.system.other'), valueClass: 'text-' + data.asic.swarmColor + '-500' },
      { label: this.i18n.t('device.system.board_version'), value: data.info.boardVersion },
      { label: this.i18n.t('device.system.asic_type'), value: (data.asic.asicCount > 1 ? data.asic.asicCount + 'x ' : ' ') + data.asic.ASICModel, class: 'pb-3' },
      { label: this.i18n.t('device.system.uptime'), value: DateAgoPipe.transform(data.info.uptimeSeconds) },
      { label: this.i18n.t('device.system.reset_reason'), value: data.info.resetReason, class: 'pb-3' },
      { label: this.i18n.t('device.system.wifi_ssid'), value: data.info.ssid, isSensitiveData: true },
      { label: this.i18n.t('device.system.wifi_status'), value: data.info.wifiStatus },
      { label: this.i18n.t('device.system.wifi_rssi.label'), value: data.info.wifiRSSI + ' dBm', valueClass: this.getWifiRssiColor(data.info.wifiRSSI), tooltip: this.getWifiRssiTooltip(data.info.wifiRSSI) },
      { label: this.i18n.t('device.system.wifi_ipv4'), value: data.info.ipv4},
      { label: this.i18n.t('device.system.wifi_ipv6'), value: data.info.ipv6, class: 'pb-3', isSensitiveData: true},
      { label: this.i18n.t('device.system.mac_address'), value: data.info.macAddr, class: 'pb-3', isSensitiveData: true },
      { label: this.i18n.t('device.system.free_heap'), value: ByteSuffixPipe.transform(data.info.freeHeap)},
      { label: this.i18n.t('device.system.free_heap_internal'), value: ByteSuffixPipe.transform(data.info.freeHeapInternal)},
      { label: this.i18n.t('device.system.free_heap_spiram'), value: ByteSuffixPipe.transform(data.info.freeHeapSpiram), class: 'pb-3' },
      { label: this.i18n.t('device.system.firmware_version'), value: data.info.version },
      { label: this.i18n.t('device.system.axeos_version'), value: data.info.axeOSVersion },
      { label: this.i18n.t('device.system.idf_version'), value: data.info.idfVersion },
    ];
  }

  identifyDevice(): void {
    this.systemService.identify()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: (result) => {
          this.toastr.success((result as GenericResponse).message);
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(this.i18n.t('errors.identify_failed', { error: err.message }));
        }
      });
  }
}
