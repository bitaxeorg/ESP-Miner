import { HttpClient } from '@angular/common/http';
import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators, FormControl } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { catchError, from, map, mergeMap, of, take, timeout, toArray } from 'rxjs';
import { LocalStorageService } from 'src/app/local-storage.service';
import { SystemService } from 'src/app/services/system.service';
const SWARM_DATA = 'SWARM_DATA'
const SWARM_REFRESH_TIME = 'SWARM_REFRESH_TIME';
const SWARM_SORTING = 'SWARM_SORTING'

@Component({
  selector: 'app-swarm',
  templateUrl: './swarm.component.html',
  styleUrls: ['./swarm.component.scss']
})
export class SwarmComponent implements OnInit, OnDestroy {

  public swarm: any[] = [];

  public selectedAxeOs: any = null;
  public showEdit = false;

  public form: FormGroup;

  public scanning = false;

  public refreshIntervalRef!: number;
  public refreshIntervalTime = 30;
  public refreshTimeSet = 30;

  public totals: { hashRate: number, power: number, bestDiff: string } = { hashRate: 0, power: 0, bestDiff: '0' };

  public isRefreshing = false;

  public refreshIntervalControl: FormControl;

  public sortField: string = '';
  public sortDirection: 'asc' | 'desc' = 'asc';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
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
      this.refreshList();
    }

    this.refreshIntervalRef = window.setInterval(() => {
      if (!this.scanning && !this.isRefreshing) {
        this.refreshIntervalTime--;
        if (this.refreshIntervalTime <= 0) {
          this.refreshList();
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
    from(ips).pipe(
      mergeMap(ipAddr =>
        this.httpClient.get(`http://${ipAddr}/api/system/info`).pipe(
          map(result => {
            if ('hashRate' in result) {
              return {
                IP: ipAddr,
                ...result
              };
            }
            return null;
          }),
          timeout(5000), // Set the timeout to 5 seconds
          catchError(error => {
            //console.error(`Request to ${ipAddr}/api/system/info failed or timed out`, error);
            return []; // Return an empty result or handle as desired
          })
        ),
        128 // Limit concurrency to avoid overload
      ),
      toArray() // Collect all results into a single array
    ).pipe(take(1)).subscribe({
      next: (result) => {
        // Filter out null items first
        const validResults = result.filter((item): item is NonNullable<typeof item> => item !== null);
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

  public add() {
    const newIp = this.form.value.manualAddIp;

    // Check if IP already exists
    if (this.swarm.some(item => item.IP === newIp)) {
      this.toastr.warning('This IP address already exists in the swarm', 'Duplicate Entry');
      return;
    }

    this.systemService.getInfo(`http://${newIp}`).subscribe((res) => {
      if (res.ASICModel) {
        this.swarm.push({ IP: newIp, ...res });
        this.sortSwarm();
        this.localStorageService.setObject(SWARM_DATA, this.swarm);
        this.calculateTotals();
      }
    });
  }

  public edit(axe: any) {
    this.selectedAxeOs = axe;
    this.showEdit = true;
  }

  public restart(axe: any) {
    this.systemService.restart(`http://${axe.IP}`).pipe(
      catchError(error => {
        this.toastr.error(`Failed to restart device at ${axe.IP}`, 'Error');
        return of(null);
      })
    ).subscribe(res => {
      if (res !== null) {
        this.toastr.success(`Bitaxe at ${axe.IP} restarted`, 'Success');
      }
    });
  }

  public remove(axeOs: any) {
    this.swarm = this.swarm.filter(axe => axe.IP != axeOs.IP);
    this.localStorageService.setObject(SWARM_DATA, this.swarm);
    this.calculateTotals();
  }

  public refreshList() {
    if (this.scanning) {
      return;
    }

    this.refreshIntervalTime = this.refreshTimeSet;
    const ips = this.swarm.map(axeOs => axeOs.IP);
    this.isRefreshing = true;

    from(ips).pipe(
      mergeMap(ipAddr =>
        this.httpClient.get(`http://${ipAddr}/api/system/info`).pipe(
          map(result => {
            return {
              IP: ipAddr,
              ...result
            }
          }),
          timeout(5000),
          catchError(error => {
            const errorMessage = error?.message || error?.statusText || error?.toString() || 'Unknown error';
            this.toastr.error('Failed to get info from ' + ipAddr, errorMessage);
            // Return existing device with zeroed stats instead of the previous state
            const existingDevice = this.swarm.find(axeOs => axeOs.IP === ipAddr);
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
            });
          })
        ),
        128 // Limit concurrency to avoid overload
      ),
      toArray() // Collect all results into a single array
    ).pipe(take(1)).subscribe({
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

  private sortByIp(a: any, b: any): number {
    return this.ipToInt(a.IP) - this.ipToInt(b.IP);
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
      } else {
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

  hasModel(model: string): string {
    return this.swarm.some(axe => axe.ASICModel === model) ? '1' : '0.5';
  }

  hasMultipleChips(): string {
    return this.swarm.some(axe => axe.asicCount > 1) ? '1' : '0.5';
  }

}
