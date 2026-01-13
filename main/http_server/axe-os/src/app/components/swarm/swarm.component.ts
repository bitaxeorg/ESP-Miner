import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { Component, OnDestroy, OnInit, ViewChild, HostListener } from '@angular/core';
import { FormBuilder, FormGroup, Validators, FormControl } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { forkJoin, catchError, from, map, mergeMap, of, take, timeout, toArray, Observable, Subscription } from 'rxjs';
import { LocalStorageService } from 'src/app/local-storage.service';
import { LayoutService } from "../../layout/service/app.layout.service";
import { SystemService } from 'src/app/services/system.service';
import { ModalComponent } from '../modal/modal.component';
import { ISystemInfo } from 'src/models/ISystemInfo';

const SWARM_DATA = 'SWARM_DATA';
const SWARM_REFRESH_TIME = 'SWARM_REFRESH_TIME';
const SWARM_SORTING = 'SWARM_SORTING';
const SWARM_GRID_VIEW = 'SWARM_GRID_VIEW';

type SwarmDevice = { 
  address: string; 
  ASICModel: string; 
  deviceModel: string; 
  swarmColor: string; 
  asicCount: number; 
  displayName?: string; 
  connectionAddress?: string; 
  [key: string]: any 
};

@Component({
  selector: 'app-swarm',
  templateUrl: './swarm.component.html',
  styleUrls: ['./swarm.component.scss']
})
export class SwarmComponent implements OnInit, OnDestroy {

  @ViewChild(ModalComponent) modalComponent!: ModalComponent;

  public swarm: any[] = [];

  public selectedAxeOs: any = null;

  public form: FormGroup;

  public scanning = false;

  public refreshIntervalRef!: number;
  public refreshIntervalTime = 30;
  public refreshTimeSet = 30;

  public totals: { hashRate: number; power: number; bestDiff: number } = { hashRate: 0, power: 0, bestDiff: 0 };

  public isRefreshing = false;

  public refreshIntervalControl: FormControl;

  public gridView: boolean;
  public selectedSort: { sortField: string; sortDirection: 'asc' | 'desc' };

  public staticMenuDesktopInactive: boolean;
  private staticMenuDesktopSubscription!: Subscription;

  public filterText = '';

  public currentDeviceIp: string | null = null;

  @HostListener('document:keydown.esc', ['$event'])
  onEscKey() {
    if (this.filterText) {
      this.filterText = '';
    }
  }

  constructor(
    private fb: FormBuilder,
    private toastr: ToastrService,
    private localStorageService: LocalStorageService,
    public layoutService: LayoutService,
    private systemService: SystemService,
    private httpClient: HttpClient
  ) {

    this.form = this.fb.group({
      manualAddAddress: [null, [Validators.required, Validators.pattern('^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)|[a-zA-Z0-9-]+(?:\\.local)?$')]]
    });

    this.gridView = this.localStorageService.getBool(SWARM_GRID_VIEW);

    const storedRefreshTime = this.localStorageService.getNumber(SWARM_REFRESH_TIME) ?? 30;
    this.refreshIntervalTime = storedRefreshTime;
    this.refreshTimeSet = storedRefreshTime;
    this.refreshIntervalControl = new FormControl(storedRefreshTime);

    this.refreshIntervalControl.valueChanges.subscribe(value => {
      this.refreshIntervalTime = value;
      this.refreshTimeSet = value;
      this.localStorageService.setNumber(SWARM_REFRESH_TIME, value);
    });


    this.selectedSort = this.localStorageService.getObject(SWARM_SORTING) ?? {
      sortField: 'address',
      sortDirection: 'asc'
    };

    this.staticMenuDesktopInactive = this.layoutService.state.staticMenuDesktopInactive;
  }

  ngOnInit(): void {
    const swarmData = this.localStorageService.getObject(SWARM_DATA);

    if (swarmData == null) {
      this.scanNetwork();
    } else {
      this.swarm = swarmData;
      this.refreshList(true);
    }

    this.staticMenuDesktopSubscription = this.layoutService.getStaticMenuDesktopInactive$()
      .subscribe(inactive => {
        this.staticMenuDesktopInactive = inactive;
      });

    this.refreshIntervalRef = window.setInterval(() => {
      if (!this.scanning && !this.isRefreshing && this.swarm.length) {
        this.refreshIntervalTime--;
        if (this.refreshIntervalTime <= 0) {
          this.refreshList(false);
        }
      }
    }, 1000);

    // Fetch current device IP for isThisDevice
    this.httpClient.get(`http://${window.location.hostname}/api/system/info`).subscribe({
      next: (response: any) => this.currentDeviceIp = response.ip,
      error: () => this.currentDeviceIp = null
    });
  }

  ngOnDestroy(): void {
    this.staticMenuDesktopSubscription.unsubscribe();
    window.clearInterval(this.refreshIntervalRef);
    this.form.reset();
  }

  private ipToInt(ip: string): number {
    return ip.split('.').reduce((acc, octet) => (acc << 8) + parseInt(octet, 10), 0) >>> 0;
  }

private isIpAddress(value: string): boolean {
    const ipRegex = /^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;
    return ipRegex.test(value);
  }

  // Utility method to get the display name for a device
  public getDeviceDisplayName(device: SwarmDevice): string {
    return device.displayName || device.address;
  }

  private intToIp(int: number): string {
    return `${(int >>> 24) & 255}.${(int >>> 16) & 255}.${(int >>> 8) & 255}.${int & 255}`;
  }

  private calculateIpRange(ip: string, netmask: string): { start: number, end: number } {
    const ipInt = this.ipToInt(ip);
    const netmaskInt = this.ipToInt(netmask);
    const network = ipInt & netmaskInt;
    const broadcast = network | ~netmaskInt;
    return { start: network + 1, end: broadcast - 1 };
  }

  scanNetwork() {
    this.scanning = true;

    if (this.isIpAddress(window.location.hostname)) {
      // Direct IP access - scan the subnet
      const { start, end } = this.calculateIpRange(window.location.hostname, '255.255.255.0');
      const ips = Array.from({ length: end - start + 1 }, (_, i) => this.intToIp(start + i));
      this.performNetworkScan(ips);
    } else {
      // mDNS hostname - fetch server IP first, then scan its subnet
      this.httpClient.get(`http://${window.location.hostname}/api/system/info`)
        .subscribe({
          next: (response: any) => {
            const serverIp = response.ip;
            const { start, end } = this.calculateIpRange(serverIp, '255.255.255.0');
            const ips = Array.from({ length: end - start + 1 }, (_, i) => this.intToIp(start + i));
            this.performNetworkScan(ips);
          },
          error: () => {
            // Fallback: skip scanning if we can't get the IP
            this.scanning = false;
          }
        });
    }
  }

  private performNetworkScan(ips: string[]) {
    this.getAllDeviceInfo(ips, () => of(null)).subscribe({
      next: (result) => {
        // Filter out null items first
        const validResults = result.filter((item): item is SwarmDevice => item !== null);
        // Merge new results with existing swarm entries
        const existingAddresses = new Set([...this.swarm.map(item => item.address), ...this.swarm.map(item => item.connectionAddress)]);
        const newItems = validResults.filter(item => {
          const isDuplicate = existingAddresses.has(item['hostname']) || existingAddresses.has(item['ip']);
          return !isDuplicate;
        });
        this.swarm = [...this.swarm, ...newItems];
        this.sortSwarm();
        this.localStorageService.setObject(SWARM_DATA, this.swarm);
        this.calculateTotals();
      },
      complete: () => {
        this.scanning = false;
        this.refreshIntervalTime = this.refreshTimeSet;
      }
    });
  }

  private getAllDeviceInfo(addresses: string[], errorHandler: (error: any, address: string) => Observable<SwarmDevice[] | null>, fetchAsic: boolean = true) {
    return from(addresses).pipe(
      mergeMap(address => forkJoin({
        info: this.httpClient.get(`http://${address}/api/system/info`).pipe(catchError(() => of(null))),
        asic: fetchAsic ? this.httpClient.get(`http://${address}/api/system/asic`).pipe(catchError(() => of({}))) : of({})
      }).pipe(
        map(({ info, asic }) => {
          // Skip processing if failed silently
          if (info === null) {
            return null;
          }

 const existingDevice = this.swarm.find(device => device.address === address);
           const result = {
             address: (info as any)['fullHostname'] || (info as any)['hostname'] || address, // Use mDNS hostname for links
             displayName: (info as any)['hostname'] ? (info as any)['hostname'].replace(/\.local$/i, '') : address,
             connectionAddress: address, // IP for API calls
             ...(existingDevice ? existingDevice : {}),
             ...info,
             ...asic,
              ...this.numerizeDeviceBestDiffs(info as ISystemInfo)
           };
          return this.fallbackDeviceModel(result);
        }),
        timeout(5000),
        catchError(error => errorHandler(error, address))
      ),
        128
      ),
      toArray()
    ).pipe(take(1));
  }

  public add() {
    const address = this.form.value.manualAddAddress;

    forkJoin({
      info: this.httpClient.get<any>(`http://${address}/api/system/info`).pipe(catchError(error => {
        if (error.status === 401 || error.status === 0) {
          this.toastr.warning(`Potential swarm peer detected at ${address} - upgrade its firmware to be able to add it.`);
          return of({ _corsError: 401 });
        }
        throw error;
      })),
      asic: this.httpClient.get<any>(`http://${address}/api/system/asic`).pipe(catchError(() => of({})))
    }).subscribe(({ info, asic }) => {
      if ((info as any)._corsError === 401) {
        return; // Already showed warning
      }
      if (!info.ASICModel || !asic.ASICModel) {
        return;
      }

      // Check if the device is already in swarm by IP
      if (this.swarm.some(item => item.connectionAddress === info['ip'])) {
        this.toastr.warning('Device already added to the swarm.', `Device at ${address}`);
        return;
      }

      // Store device with proper display name and address for connectivity
       const device = {
         address: info['fullHostname'] || info['hostname'] || address, // Use mDNS hostname for links
         displayName: info['hostname'] ? info['hostname'].replace(/\.local$/i, '') : address,
         connectionAddress: info['ip'] || address, // IP for API calls
         ...asic,
         ...info,
         ...this.numerizeDeviceBestDiffs(info)
       };
      this.swarm.push(device);
      this.sortSwarm();
      this.localStorageService.setObject(SWARM_DATA, this.swarm);
      this.calculateTotals();
    });
  }

  public edit(device: any) {
    this.selectedAxeOs = device;
    this.modalComponent.isVisible = true;
  }

  public postAction(device: any, action: string) {
    this.httpClient.post(`http://${device.connectionAddress}/api/system/${action}`, {}, { responseType: 'text' }).pipe(
      timeout(800),
      catchError(error => {
        let errorMsg = `Failed to ${action} device at ${device.address}`;
        if (error.name === 'TimeoutError') {
          errorMsg = 'Request timed out';
        } else if (error.message) {
          errorMsg += `: ${error.message}`;
        }
        this.toastr.error(errorMsg, `Device at ${device.address}`);
        return of(null);
      })
    ).subscribe(res => {
      if (res !== null) {
        this.toastr.success(res, `Device at ${device.address}`);
      }
    });
  }

  public remove(device: any) {
    this.swarm = this.swarm.filter(axe => axe.address !== device.address);
    this.localStorageService.setObject(SWARM_DATA, this.swarm);
    this.calculateTotals();
  }

  public refreshErrorHandler = (error: any, address: string) => {
    const errorMessage = error?.message || error?.statusText || error?.toString() || 'Unknown error';
    this.toastr.error(`Failed to get info: ${errorMessage}`, `Device at ${address}`);
    const existingDevice = this.swarm.find(axeOs => axeOs.connectionAddress === address);
    return of({
      ...existingDevice,
      hashRate: 0,
      sharesAccepted: 0,
      power: 0,
      voltage: 0,
      temp: 0,
      bestDiff: 0,
      version: 0,
      uptimeSeconds: 0,
      poolDifficulty: 0,
    });
  };

  public refreshList(fetchAsic: boolean = true) {
    if (this.scanning) {
      return;
    }

    this.refreshIntervalTime = this.refreshTimeSet;
    const addresses = this.swarm.filter(Boolean).map(axeOs => axeOs.connectionAddress);
    this.isRefreshing = true;

    this.getAllDeviceInfo(addresses, this.refreshErrorHandler, fetchAsic).subscribe({
      next: (result) => {
        this.swarm = result;
        this.sortSwarm();
        this.localStorageService.setObject(SWARM_DATA, this.swarm);
        this.calculateTotals();
        this.isRefreshing = false;
      },
      complete: () => {
        this.isRefreshing = false;
      }
    });
  }

  sortBy(sortField: string, sortDirection?: 'asc' | 'desc' | undefined) {
    if (sortDirection) {
      this.selectedSort = { sortField, sortDirection };
    } else if (this.selectedSort.sortField === sortField) {
      this.selectedSort = { sortField, sortDirection: this.selectedSort.sortDirection === 'asc' ? 'desc' : 'asc' };
    } else {
      this.selectedSort = { sortField, sortDirection: 'asc' };
    }

    this.localStorageService.setObject(SWARM_SORTING, this.selectedSort);
    this.sortSwarm();
  }

  private sortSwarm() {
    this.swarm.sort((a, b) => {
      let comparison = 0;
      const fieldType = typeof a[this.selectedSort.sortField];

      if (this.selectedSort.sortField === 'address') {
        const aValue = a[this.selectedSort.sortField];
        const bValue = b[this.selectedSort.sortField];
        const aIsIp = this.isIpAddress(aValue);
        const bIsIp = this.isIpAddress(bValue);

        if (aIsIp && bIsIp) {
          // Both are IPs, sort numerically
          const aOctets = aValue.split('.').map(Number);
          const bOctets = bValue.split('.').map(Number);
          for (let i = 0; i < 4; i++) {
            if (aOctets[i] !== bOctets[i]) {
              comparison = aOctets[i] - bOctets[i];
              break;
            }
          }
        } else if (!aIsIp && !bIsIp) {
          // Both are hostnames, sort alphabetically
          comparison = aValue.localeCompare(bValue);
        } else {
          // Mixed, sort IPs before hostnames
          comparison = aIsIp ? -1 : 1;
        }
      } else if (fieldType === 'number') {
        comparison = a[this.selectedSort.sortField] - b[this.selectedSort.sortField];
      } else if (fieldType === 'string') {
        comparison = a[this.selectedSort.sortField].localeCompare(b[this.selectedSort.sortField], undefined, { numeric: true });
      }
      return this.selectedSort.sortDirection === 'asc' ? comparison : -comparison;
    });
  }

  private calculateTotals() {
    this.totals.hashRate = this.swarm.reduce((sum, axe) => sum + (axe.hashRate || 0), 0);
    this.totals.power = this.swarm.reduce((sum, axe) => sum + (axe.power || 0), 0);
    this.totals.bestDiff = this.swarm.reduce((max, axe) => Math.max(max, axe.bestDiff || 0), 0);
  }

  get deviceFamilies(): SwarmDevice[] {
    return this.filteredSwarm.filter(Boolean).filter((v, i, a) =>
      a.findIndex(({ deviceModel, ASICModel, asicCount }) =>
        v.deviceModel === deviceModel &&
        v.ASICModel === ASICModel &&
        v.asicCount === asicCount
      ) === i
    );
  }

  // Fallback logic to derive deviceModel and swarmColor, can be removed after some time
  private fallbackDeviceModel(data: any): any {
    if (data.deviceModel && data.swarmColor && data.poolDifficulty && data.hashRate) return data;
    const deviceModel = data.deviceModel || this.deriveDeviceModel(data);
    const swarmColor = data.swarmColor || this.deriveSwarmColor(deviceModel);
    const poolDifficulty = data.poolDifficulty || data.stratumDiff;
    const hashRate = data.hashRate || data.hashRate_10m;
    return { ...data, deviceModel, swarmColor, poolDifficulty, hashRate };
  }

  private deriveDeviceModel(data: any): string {
    if (data.boardVersion && data.boardVersion.length > 1) {
      if (data.boardVersion[0] == "1" || data.boardVersion == "2.2") return "Max";
      if (data.boardVersion[0] == "2" || data.boardVersion == "0.11") return "Ultra";
      if (data.boardVersion[0] == "3") return "UltraHex";
      if (data.boardVersion[0] == "4") return "Supra";
      if (data.boardVersion[0] == "6") return "Gamma";
      if (data.boardVersion[0] == "8") return "GammaTurbo";
    }
    return 'Other';
  }

  private deriveSwarmColor(deviceModel: string): string {
    switch (deviceModel) {
      case 'Max':        return 'red';
      case 'Ultra':      return 'purple';
      case 'Supra':      return 'blue';
      case 'UltraHex':   return 'orange';
      case 'Gamma':      return 'green';
      case 'GammaTurbo': return 'cyan';
      default:           return 'gray';
    }
  }

  private numerizeDeviceBestDiffs(info: ISystemInfo) {
    const parseAsNumber = (val: number | string): number => {
      return typeof val === 'string' ? this.parseSuffixString(val) : val;
    };

    return {
      bestDiff: parseAsNumber(info.bestDiff),
      bestSessionDiff: parseAsNumber(info.bestSessionDiff),
    };
  }

  private parseSuffixString(input: string): number {
    input = input.trim();
    const value = parseFloat(input);
    const lastChar = input.charAt(input.length - 1).toUpperCase();

    const multipliers: Record<string, number> = {
      K: 1e3,
      M: 1e6,
      G: 1e9,
      T: 1e12,
      P: 1e15,
      E: 1e18,
    };

    const multiplier = multipliers[lastChar] ?? 1;

    return value * multiplier;
  }

  public stringifyDeviceLabel(data: any): string {
    const model = data.deviceModel || 'Other';
    const asicCountPart = data.asicCount > 1 ? data.asicCount + 'x ' : '';
    const asicModel = data.ASICModel || '';

    return model + ' (' + asicCountPart + asicModel + ')';
  };


  public toggleGridView(gridView: boolean): void {
    this.localStorageService.setBool(SWARM_GRID_VIEW, this.gridView = gridView);
  }

  get sortOptions() {
    return [
      { label: 'Hostname (Z-A)', value: { sortField: 'hostname', sortDirection: 'desc' } },
      { label: 'Hostname (A-Z)', value: { sortField: 'hostname', sortDirection: 'asc' } },
      { label: 'Address', value: { sortField: 'address', sortDirection: 'desc' } },
      { label: 'Address', value: { sortField: 'address', sortDirection: 'asc' } },
      { label: 'Hashrate', value: { sortField: 'hashRate', sortDirection: 'desc' } },
      { label: 'Hashrate', value: { sortField: 'hashRate', sortDirection: 'asc' } },
      { label: 'Shares', value: { sortField: 'sharesAccepted', sortDirection: 'desc' } },
      { label: 'Shares', value: { sortField: 'sharesAccepted', sortDirection: 'asc' } },
      { label: 'Best Diff', value: { sortField: 'bestDiff', sortDirection: 'desc' } },
      { label: 'Best Diff', value: { sortField: 'bestDiff', sortDirection: 'asc' } },
      { label: 'Uptime', value: { sortField: 'uptimeSeconds', sortDirection: 'desc' } },
      { label: 'Uptime', value: { sortField: 'uptimeSeconds', sortDirection: 'asc' } },
      { label: 'Power', value: { sortField: 'power', sortDirection: 'desc' } },
      { label: 'Power', value: { sortField: 'power', sortDirection: 'asc' } },
      { label: 'Temp', value: { sortField: 'temp', sortDirection: 'desc' } },
      { label: 'Temp', value: { sortField: 'temp', sortDirection: 'asc' } },
      { label: 'Pool Diff', value: { sortField: 'poolDifficulty', sortDirection: 'desc' } },
      { label: 'Pool Diff', value: { sortField: 'poolDifficulty', sortDirection: 'asc' } },
      { label: 'Version', value: { sortField: 'version', sortDirection: 'desc' } },
      { label: 'Version', value: { sortField: 'version', sortDirection: 'asc' } },
    ];
  }

  onSortChange(event: {value: {sortField: string; sortDirection: 'asc' | 'desc'}}) {
    const {sortField, sortDirection} = event.value;

    this.sortBy(sortField, sortDirection);
  }

  get filteredSwarm() {
    if (!this.filterText) {
      return this.swarm;
    }

    const filter = this.filterText.toLowerCase();
return this.swarm.filter(axe =>
      this.getDeviceDisplayName(axe).toLowerCase().includes(filter) ||
      axe.ASICModel.toLowerCase().includes(filter) ||
      axe.deviceModel.toLowerCase().includes(filter) ||
      axe.address.toLowerCase().includes(filter)
    );
  }

  getDeviceNotification(axe: any): { color: string; msg: string } | undefined {
    switch (true) {
      case axe.overheat_mode === 1:
        return { color: 'red', msg: 'Overheated' };
      case !!axe.power_fault:
        return { color: 'red', msg: 'Power Fault' };
      case !axe.frequency || axe.frequency < 400:
        return { color: 'orange', msg: 'Frequency Low' };
      case axe.isUsingFallbackStratum === 1:
        return { color: 'orange', msg: 'Fallback Pool' };
      case axe.blockFound === 1:
        return { color: 'green', msg: 'Block found' };
      default:
        return undefined;
    }
  }

  isThisDevice(IP: string): boolean {
    return IP === window.location.hostname || (this.currentDeviceIp !== null && IP === this.currentDeviceIp);
  }
}
