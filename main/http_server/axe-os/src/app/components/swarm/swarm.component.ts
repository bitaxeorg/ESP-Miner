import { HttpClient } from '@angular/common/http';
import { Component, OnDestroy, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { BehaviorSubject, catchError, combineLatest, debounce, debounceTime, forkJoin, from, interval, map, mergeAll, mergeMap, Observable, of, startWith, switchMap, take, timeout, toArray } from 'rxjs';
import { LocalStorageService } from 'src/app/local-storage.service';
import { SystemService } from 'src/app/services/system.service';
const REFRESH_TIME_SECONDS = 30;
const SWARM_DATA = 'SWARM_DATA'
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
  public refreshIntervalTime = REFRESH_TIME_SECONDS;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private localStorageService: LocalStorageService,
    private httpClient: HttpClient
  ) {

    this.form = this.fb.group({
      manualAddIp: [null, [Validators.required, Validators.pattern('(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)')]]
    })

  }

  ngOnInit(): void {
    const swarmData = this.localStorageService.getObject(SWARM_DATA);
    console.log(swarmData);
    if (swarmData == null) {
      this.scanNetwork();
      //this.swarm$ = this.scanNetwork('192.168.1.23', '255.255.255.0').pipe(take(1));
    } else {
      this.swarm = swarmData;
    }

    this.refreshIntervalRef = window.setInterval(() => {
      this.refreshIntervalTime --;
      if(this.refreshIntervalTime <= 0){
        this.refreshIntervalTime = REFRESH_TIME_SECONDS;
        this.refreshList();
      }
    }, 1000);
  }

  ngOnDestroy(): void {
    window.clearInterval(this.refreshIntervalRef);
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
            return {
              IP: ipAddr,
              ...result
            }
          }),
          timeout(5000), // Set the timeout to 1 second
          catchError(error => {
            //console.error(`Request to ${ipAddr}/api/system/info failed or timed out`, error);
            return []; // Return an empty result or handle as desired
          })
        ),
        256 // Limit concurrency to avoid overload
      ),
      toArray() // Collect all results into a single array
    ).pipe(take(1)).subscribe({
      next: (result) => {
        this.swarm = result;
        this.localStorageService.setObject(SWARM_DATA, this.swarm);
      },
      complete: () => {
        this.scanning = false;
      }
    });
  }

  public add() {
    const newIp = this.form.value.manualAddIp;

    this.systemService.getInfo(`http://${newIp}`).subscribe((res) => {
      if (res.ASICModel) {
        this.swarm.push({ IP: newIp, ...res });
        this.localStorageService.setObject(SWARM_DATA, this.swarm);
      }
    });
  }

  public edit(axe: any) {
    this.selectedAxeOs = axe;
    this.showEdit = true;
  }

  public restart(axe: any) {
    this.systemService.restart(`http://${axe.IP}`).subscribe(res => {

    });
    this.toastr.success('Success!', 'Bitaxe restarted');
  }

  public remove(axeOs: any) {
    this.swarm = this.swarm.filter(axe => axe.IP != axeOs.IP);
    this.localStorageService.setObject(SWARM_DATA, this.swarm);
  }

  public refreshList() {
    const ips = this.swarm.map(axeOs => axeOs.IP);

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
            return of(this.swarm.find(axeOs => axeOs.IP == ipAddr));
          })
        ),
        256 // Limit concurrency to avoid overload
      ),
      toArray() // Collect all results into a single array
    ).pipe(take(1)).subscribe({
      next: (result) => {
        this.swarm = result;
        this.localStorageService.setObject(SWARM_DATA, this.swarm);
      },
      complete: () => {
      }
    });

  }

}
