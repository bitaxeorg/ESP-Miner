import { Component, OnInit, OnDestroy } from '@angular/core';
import { Observable, Subject, combineLatest, switchMap, shareReplay, first, takeUntil, map, timer } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';
import { LoadingService } from 'src/app/services/loading.service';
import { DateAgoPipe } from 'src/app/pipes/date-ago.pipe';
import { ByteSuffixPipe } from 'src/app/pipes/byte-suffix.pipe';
import { ISystemInfo } from 'src/models/ISystemInfo';
import { ISystemASIC } from 'src/models/ISystemASIC';

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
    private systemService: SystemService,
    private loadingService: LoadingService,
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
    if (rssi > -50) return 'Excellent';
    if (rssi <= -50 && rssi > -60) return 'Good';
    if (rssi <= -60 && rssi > -70) return 'Fair';

    return 'Weak';
  }

  getSystemRows(data: CombinedData): TableRow[] {
    return [
      { label: 'Device Model', value: data.asic.deviceModel || 'Other', valueClass: 'text-' + data.asic.swarmColor + '-500' },
      { label: 'Board Version', value: data.info.boardVersion },
      { label: 'ASIC Type', value: (data.asic.asicCount > 1 ? data.asic.asicCount + 'x ' : ' ') + data.asic.ASICModel, class: 'pb-3' },
      { label: 'Uptime', value: DateAgoPipe.transform(data.info.uptimeSeconds), class: 'pb-3' },
      { label: 'Wi-Fi SSID', value: data.info.ssid, isSensitiveData: true },
      { label: 'Wi-Fi Status', value: data.info.wifiStatus },
      { label: 'Wi-Fi RSSI', value: data.info.wifiRSSI + ' dBm', valueClass: this.getWifiRssiColor(data.info.wifiRSSI), tooltip: this.getWifiRssiTooltip(data.info.wifiRSSI) },
      { label: 'Wi-Fi IPv4', value: data.info.ipv4},
      { label: 'Wi-Fi IPv6', value: data.info.ipv6, class: 'pb-3', isSensitiveData: true},
      { label: 'MAC Address', value: data.info.macAddr, class: 'pb-3', isSensitiveData: true },
      { label: 'Free Heap Memory', value: ByteSuffixPipe.transform(data.info.freeHeap)},
      { label: '• Internal', value: ByteSuffixPipe.transform(data.info.freeHeapInternal)},
      { label: '• Spiram', value: ByteSuffixPipe.transform(data.info.freeHeapSpiram), class: 'pb-3' },
      { label: 'Firmware Version', value: data.info.version },
      { label: 'AxeOS Version', value: data.info.axeOSVersion },
      { label: 'ESP-IDF Version', value: data.info.idfVersion },
    ];
  }
}
