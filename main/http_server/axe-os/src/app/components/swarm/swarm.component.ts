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

  public showManualAddition: boolean = false;

  constructor(
    private fb: FormBuilder,
    private toastr: ToastrService,
    private localStorageService: LocalStorageService,
    private httpClient: HttpClient
  ) {

    this.form = this.fb.group({
      manualAddIp: [null, [Validators.required, Validators.pattern('^(?:(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?))|(?:(?:[a-zA-Z0-9](?:[a-zA-Z0-9\\-]{0,61}[a-zA-Z0-9])?\\.)*[a-zA-Z0-9](?:[a-zA-Z0-9\\-]{0,61}[a-zA-Z0-9])?)$')]],
      manualAddApiSecret: ['', [Validators.minLength(12), Validators.maxLength(32)]]
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


  scanNetwork() {
    this.scanning = true;

    // Call the mDNS scan endpoint
    this.httpClient.get<any[]>('/api/system/network/scan').subscribe({
      next: (foundDevices) => {
        if (!Array.isArray(foundDevices)) {
          this.toastr.error('Invalid response from scan endpoint', 'Scan Error');
          this.scanning = false;
          return;
        }

        // Filter out devices we already have - check both IP and currentIP
        const existingIps = new Set([...this.swarm.map(item => item.IP), ...this.swarm.map(item => item.currentIP)]);
        const existingHostnames = new Set(this.swarm.map(item => item.hostname).filter(h => h));

        const newDevices = foundDevices.filter(device => {
          // Check if device IP matches any existing IP or currentIP
          const ipNotExists = !existingIps.has(device.IP);
          const hostnameNotExists = !device.hostname || !existingHostnames.has(device.hostname);
          return ipNotExists && hostnameNotExists;
        });

        if (newDevices.length === 0) {
          this.toastr.info('No new devices found', 'Network Scan Complete');
          this.scanning = false;
          return;
        }

        // Fetch device info for each discovered device
        const deviceInfoRequests = newDevices.map(device => {
          const httpOptions = this.getHttpOptionsForDevice(device);

          return forkJoin({
            info: this.httpClient.get<any>(`http://${device.IP}/api/system/info`, httpOptions),
            asic: this.httpClient.get<any>(`http://${device.IP}/api/system/asic`, httpOptions).pipe(catchError(() => of({})))
          }).pipe(
            map(({ info, asic }) => {
              // Merge mDNS data with fetched info
              return { ...device, ...info, ...asic };
            }),
            timeout(5000),
            catchError(error => {
              this.toastr.error(`Failed to get info from ${device.IP}`, 'Device Error');
              return of(null);
            })
          );
        });

        forkJoin(deviceInfoRequests).subscribe({
          next: (devices) => {
            const validDevices = devices.filter(d => d !== null && d.ASICModel);

            // Add new devices to swarm
            this.swarm = [...this.swarm, ...validDevices];
            this.sortSwarm();
            this.localStorageService.setObject(SWARM_DATA, this.swarm);
            this.calculateTotals();

            this.toastr.success(`Found ${validDevices.length} new device(s)`, 'Network Scan Complete');
            this.scanning = false;
          },
          error: (error) => {
            this.toastr.error(`Failed to fetch device information: ${error.message || 'Unknown error'}`, 'Scan Error');
            this.scanning = false;
          }
        });
      },
      error: (error) => {
        this.toastr.error(`Scan failed: ${error.message || 'Unknown error'}`, 'Scan Error');
        this.scanning = false;
      }
    });
  }


  private getHttpOptionsForDevice(device: any): { headers: any } {
    const headers: any = {
      'X-Requested-With': 'XMLHttpRequest'
    };

    // Check if device has an API secret stored
    const apiSecret = device?.apiSecret;
    if (apiSecret && apiSecret.trim() !== '') {
      headers['Authorization'] = `Bearer ${apiSecret}`;
    }

    return { headers };
  }

  private getAllDeviceInfo(errorHandler: (error: any, ip: string) => Observable<SwarmDevice[] | null>, fetchAsic: boolean = true) {
    return from(this.swarm).pipe(
      mergeMap(device => {
        const httpOptions = this.getHttpOptionsForDevice(device);

        return forkJoin({
          info: this.httpClient.get(`http://${device.IP}/api/system/info`, httpOptions),
          asic: fetchAsic ? this.httpClient.get(`http://${device.IP}/api/system/asic`, httpOptions).pipe(catchError(() => of({}))) : of({})
        }).pipe(
          map(({ info, asic }) => {
            const result = { ...device, ...info, ...asic };
            return this.fallbackDeviceModel(result);
          }),
          timeout(5000),
          catchError(error => errorHandler(error, device.IP))
        );
      },
        128
      ),
      toArray()
    ).pipe(take(1));
  }

  public add() {
    const IP = this.form.value.manualAddIp;
    const apiSecret = this.form.value.manualAddApiSecret;

    // Check if IP already exists
    if (this.swarm.some(item => item.IP === IP || item.currentIP === IP)) {
      this.toastr.warning('This IP address already exists in the swarm', 'Duplicate Entry');
      return;
    }

    // Create temporary device object for header generation
    const httpOptions = this.getHttpOptionsForDevice({ IP, apiSecret });

    forkJoin({
      info: this.httpClient.get<any>(`http://${IP}/api/system/info`, httpOptions),
      asic: this.httpClient.get<any>(`http://${IP}/api/system/asic`, httpOptions).pipe(catchError(() => of({})))
    }).subscribe({
      next: ({ info, asic }) => {
        if (!info.ASICModel || !asic.ASICModel) {
          return;
        }

        // Store the API secret with the device for future requests
        const deviceData = { IP, ...asic, ...info };
        if (apiSecret && apiSecret.trim() !== '') {
          deviceData.apiSecret = apiSecret;
        }

        // Prevent an edge case where users already have a Bitaxe in the Swarm as an IP, but attempt to then add via other names
        if (this.swarm.some(item => item.IP === info.currentIP || item.currentIP === info.currentIP)) {
          this.toastr.warning('This IP address already exists in the swarm', 'Duplicate Entry');
          return;
        }

        this.swarm.push(deviceData);
        this.sortSwarm();
        this.localStorageService.setObject(SWARM_DATA, this.swarm);
        this.calculateTotals();
        this.toastr.success(`Device at ${IP} added successfully`, 'Success');
      },
      error: (error) => {
        this.toastr.error(`Failed to add device at ${IP}: ${error.message || 'Unknown error'}`, 'Error');
      }
    });
  }

  public edit(axe: any) {
    this.selectedAxeOs = axe;
    this.modalComponent.isVisible = true;
  }

  public restart(axe: any) {
    this.httpClient.post(`http://${axe.IP}/api/system/restart`, {}, this.getHttpOptionsForDevice(axe)).pipe(
      catchError(error => {
        if (error.status === 0 || error.status === 200 || error.name === 'HttpErrorResponse') {
          return of('success');
        } else {
          this.toastr.error(`Failed to restart device at ${axe.IP}`, 'Error');
          return of(null);
        }
      })
    ).subscribe(res => {
      if (res !== null && res == 'success') {
        this.toastr.success(`Bitaxe at ${axe.IP} restarted`, 'Success');
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
    this.toastr.error(`Failed to get info from ${ip}`, errorMessage);
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
    this.isRefreshing = true;

    this.getAllDeviceInfo(this.refreshErrorHandler, fetchAsic).subscribe({
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
        // Check if both are valid IP addresses (4 octets with numeric values)
        const aIsIP = /^(\d{1,3}\.){3}\d{1,3}$/.test(a[this.sortField]);
        const bIsIP = /^(\d{1,3}\.){3}\d{1,3}$/.test(b[this.sortField]);

        if (aIsIP && bIsIP) {
          // Both are IPs - split into octets and compare numerically
          const aOctets = a[this.sortField].split('.').map(Number);
          const bOctets = b[this.sortField].split('.').map(Number);
          for (let i = 0; i < 4; i++) {
            if (aOctets[i] !== bOctets[i]) {
              comparison = aOctets[i] - bOctets[i];
              break;
            }
          }
        } else if (aIsIP && !bIsIP) {
          // IP addresses come before hostnames
          comparison = -1;
        } else if (!aIsIP && bIsIP) {
          // Hostnames come after IP addresses
          comparison = 1;
        } else {
          // Both are hostnames - use locale compare
          comparison = a[this.sortField].localeCompare(b[this.sortField], undefined, { numeric: true });
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
      case 'GammaHex':   return 'lime'; // New color?
      case 'GammaTurbo': return 'cyan';
      default:           return 'gray';
    }
  }
}
