import { HttpClient } from '@angular/common/http';
import { Component, OnDestroy, OnInit, ViewChild } from '@angular/core';
import { FormBuilder, FormGroup, Validators, FormControl } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { forkJoin, catchError, from, map, mergeMap, of, take, timeout, toArray, Observable } from 'rxjs';
import { LocalStorageService } from 'src/app/local-storage.service';
import { ModalComponent } from '../modal/modal.component';

const SWARM_DATA = 'SWARM_DATA';
const SWARM_REFRESH_TIME = 'SWARM_REFRESH_TIME';
const SWARM_SORTING = 'SWARM_SORTING';

type SwarmDevice = { IP: string; ASICModel: string; deviceModel: string; swarmColor: string; asicCount: number; [key: string]: any };

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

  public totals: { hashRate: number; power: number; bestDiff: string } = { hashRate: 0, power: 0, bestDiff: '0' };

  public isRefreshing = false;

  public refreshIntervalControl: FormControl;

  public sortField: string = '';
  public sortDirection: 'asc' | 'desc' = 'asc';

  constructor(
    private fb: FormBuilder,
    private toastr: ToastrService,
    private localStorageService: LocalStorageService,
    private httpClient: HttpClient
  ) {

    this.form = this.fb.group({
      manualAddIp: [null, [Validators.required, Validators.pattern('(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)')]]
    });

    const storedRefreshTime = this.localStorageService.getNumber(SWARM_REFRESH_TIME) ?? 30;
    this.refreshIntervalTime = storedRefreshTime;
    this.refreshTimeSet = storedRefreshTime;
    this.refreshIntervalControl = new FormControl(storedRefreshTime);

    this.refreshIntervalControl.valueChanges.subscribe(value => {
      this.refreshIntervalTime = value;
      this.refreshTimeSet = value;
      this.localStorageService.setNumber(SWARM_REFRESH_TIME, value);
    });

    const storedSorting = this.localStorageService.getObject(SWARM_SORTING) ?? {
      sortField: 'IP',
      sortDirection: 'asc'
    };
    this.sortField = storedSorting.sortField;
    this.sortDirection = storedSorting.sortDirection;
  }

  ngOnInit(): void {
    const swarmData = this.localStorageService.getObject(SWARM_DATA);

    if (swarmData == null) {
      this.scanNetwork();
    } else {
      this.swarm = swarmData;
      this.refreshList(true);
    }

    this.refreshIntervalRef = window.setInterval(() => {
      if (!this.scanning && !this.isRefreshing) {
        this.refreshIntervalTime--;
        if (this.refreshIntervalTime <= 0) {
          this.refreshList(false);
        }
      }
    }, 1000);
  }

  ngOnDestroy(): void {
    window.clearInterval(this.refreshIntervalRef);
    this.form.reset();
  }

  private ipToInt(ip: string): number {
    return ip.split('.').reduce((acc, octet) => (acc << 8) + parseInt(octet, 10), 0) >>> 0;
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

    const { start, end } = this.calculateIpRange(window.location.hostname, '255.255.255.0');
    const ips = Array.from({ length: end - start + 1 }, (_, i) => this.intToIp(start + i));
    this.getAllDeviceInfo(ips, () => of(null)).subscribe({
      next: (result) => {
        // Filter out null items first
        const validResults = result.filter((item): item is SwarmDevice => item !== null);
        // Merge new results with existing swarm entries
        const existingIps = new Set(this.swarm.map(item => item.IP));
        const newItems = validResults.filter(item => !existingIps.has(item.IP));
        this.swarm = [...this.swarm, ...newItems];
        this.sortSwarm();
        this.localStorageService.setObject(SWARM_DATA, this.swarm);
        this.calculateTotals();
      },
      complete: () => {
        this.scanning = false;
      }
    });
  }

  private getAllDeviceInfo(ips: string[], errorHandler: (error: any, ip: string) => Observable<SwarmDevice[] | null>, fetchAsic: boolean = true) {
    return from(ips).pipe(
      mergeMap(IP => forkJoin({
        info: this.httpClient.get(`http://${IP}/api/system/info`),
        asic: fetchAsic ? this.httpClient.get(`http://${IP}/api/system/asic`).pipe(catchError(() => of({}))) : of({})
      }).pipe(
        map(({ info, asic }) => {
          const existingDevice = this.swarm.find(device => device.IP === IP);
          const result = { IP, ...(existingDevice ? existingDevice : {}), ...info, ...asic };
          return this.fallbackDeviceModel(result);
        }),
        timeout(5000),
        catchError(error => errorHandler(error, IP))
      ),
        128
      ),
      toArray()
    ).pipe(take(1));
  }

  public add() {
    const IP = this.form.value.manualAddIp;

    // Check if IP already exists
    if (this.swarm.some(item => item.IP === IP)) {
      this.toastr.warning('This IP address already exists in the swarm.');
      return;
    }

    forkJoin({
      info: this.httpClient.get<any>(`http://${IP}/api/system/info`),
      asic: this.httpClient.get<any>(`http://${IP}/api/system/asic`).pipe(catchError(() => of({})))
    }).subscribe(({ info, asic }) => {
      if (!info.ASICModel || !asic.ASICModel) {
        return;
      }

      this.swarm.push({ IP, ...asic, ...info });
      this.sortSwarm();
      this.localStorageService.setObject(SWARM_DATA, this.swarm);
      this.calculateTotals();
    });
  }

  public edit(axe: any) {
    this.selectedAxeOs = axe;
    this.modalComponent.isVisible = true;
  }

  public restart(axe: any) {
    this.httpClient.post(`http://${axe.IP}/api/system/restart`, {}).pipe(
      catchError(error => {
        if (error.status === 0 || error.status === 200 || error.name === 'HttpErrorResponse') {
          return of('success');
        } else {
          this.toastr.error(`Failed to restart device at ${axe.IP}`);
          return of(null);
        }
      })
    ).subscribe(res => {
      if (res !== null && res == 'success') {
        this.toastr.success(`Device at ${axe.IP} restarted`);
      }
    });
  }

  public remove(axeOs: any) {
    this.swarm = this.swarm.filter(axe => axe.IP !== axeOs.IP);
    this.localStorageService.setObject(SWARM_DATA, this.swarm);
    this.calculateTotals();
  }

  public refreshErrorHandler = (error: any, ip: string) => {
    const errorMessage = error?.message || error?.statusText || error?.toString() || 'Unknown error';
    this.toastr.error(`Failed to get info from ${ip}`);
    const existingDevice = this.swarm.find(axeOs => axeOs.IP === ip);
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
    const ips = this.swarm.map(axeOs => axeOs.IP);
    this.isRefreshing = true;

    this.getAllDeviceInfo(ips, this.refreshErrorHandler, fetchAsic).subscribe({
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

  sortBy(field: string) {
    // If clicking the same field, toggle direction
    if (this.sortField === field) {
      this.sortDirection = this.sortDirection === 'asc' ? 'desc' : 'asc';
    } else {
      // New field, set to ascending by default
      this.sortField = field;
      this.sortDirection = 'asc';
    }

    this.localStorageService.setObject(SWARM_SORTING, {
      sortField: this.sortField,
      sortDirection: this.sortDirection
    });

    this.sortSwarm();
  }

  private sortSwarm() {
    this.swarm.sort((a, b) => {
      let comparison = 0;
      const fieldType = typeof a[this.sortField];

      if (this.sortField === 'IP') {
        // Split IP into octets and compare numerically
        const aOctets = a[this.sortField].split('.').map(Number);
        const bOctets = b[this.sortField].split('.').map(Number);
        for (let i = 0; i < 4; i++) {
          if (aOctets[i] !== bOctets[i]) {
            comparison = aOctets[i] - bOctets[i];
            break;
          }
        }
      } else if (fieldType === 'number') {
        comparison = a[this.sortField] - b[this.sortField];
      } else if (fieldType === 'string') {
        comparison = a[this.sortField].localeCompare(b[this.sortField], undefined, { numeric: true });
      }
      return this.sortDirection === 'asc' ? comparison : -comparison;
    });
  }

  private compareBestDiff(a: string, b: string): string {
    if (!a || a === '0') return b || '0';
    if (!b || b === '0') return a;

    const units = 'kMGTPE';
    const unitA = units.indexOf(a.slice(-1));
    const unitB = units.indexOf(b.slice(-1));

    if (unitA !== unitB) {
      return unitA > unitB ? a : b;
    }

    const valueA = parseFloat(a.slice(0, unitA >= 0 ? -1 : 0));
    const valueB = parseFloat(b.slice(0, unitB >= 0 ? -1 : 0));
    return valueA >= valueB ? a : b;
  }

  private calculateTotals() {
    this.totals.hashRate = this.swarm.reduce((sum, axe) => sum + (axe.hashRate || 0), 0);
    this.totals.power = this.swarm.reduce((sum, axe) => sum + (axe.power || 0), 0);
    this.totals.bestDiff = this.swarm
      .map(axe => axe.bestDiff || '0')
      .reduce((max, curr) => this.compareBestDiff(max, curr), '0');
  }

  get getFamilies(): SwarmDevice[] {
    return this.swarm.filter((v, i, a) =>
      a.findIndex(({ deviceModel, ASICModel, asicCount }) =>
        v.deviceModel === deviceModel &&
        v.ASICModel === ASICModel &&
        v.asicCount === asicCount
      ) === i
    );
  }

  // Fallback logic to derive deviceModel and swarmColor, can be removed after some time
  private fallbackDeviceModel(data: any): any {
    if (data.deviceModel && data.swarmColor && data.poolDifficulty) return data;
    const deviceModel = data.deviceModel || this.deriveDeviceModel(data);
    const swarmColor = data.swarmColor || this.deriveSwarmColor(deviceModel);
    const poolDifficulty = data.poolDifficulty || data.stratumDiff;
    return { ...data, deviceModel, swarmColor, poolDifficulty };
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
}