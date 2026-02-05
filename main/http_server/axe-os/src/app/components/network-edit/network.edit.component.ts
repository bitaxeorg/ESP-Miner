import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { finalize } from 'rxjs/operators';
import { BehaviorSubject, Observable } from 'rxjs';
import { DialogService } from 'src/app/services/dialog.service';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemApiService } from 'src/app/services/system.service';

interface WifiNetwork {
  ssid: string;
  rssi: number;
  authmode: number;
}

@Component({
  selector: 'app-network-edit',
  templateUrl: './network.edit.component.html',
  styleUrls: ['./network.edit.component.scss']
})
export class NetworkEditComponent implements OnInit {
  private formSubject = new BehaviorSubject<FormGroup | null>(null);
  public form$: Observable<FormGroup | null> = this.formSubject.asObservable();

  public form!: FormGroup;
  public savedChanges: boolean = false;
  public scanning: boolean = false;

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemApiService,
    private toastr: ToastrService,
    private loadingService: LoadingService,
    private http: HttpClient,
    private dialogService: DialogService
  ) {

  }
  ngOnInit(): void {
    this.systemService.getInfo(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.form = this.fb.group({
          hostname: [info.hostname, [Validators.required]],
          ssid: [info.ssid, [Validators.required]],
          wifiPass: ['*****'],
        });
        this.formSubject.next(this.form);
      });
  }


  public updateSystem() {

    const form = this.form.getRawValue();

    // Allow an empty Wi-Fi password
    form.wifiPass = form.wifiPass == null ? '' : form.wifiPass;

    if (form.wifiPass === '*****') {
      delete form.wifiPass;
    }

    // Trim SSID to remove any leading/trailing whitespace
    if (form.ssid) {
      form.ssid = form.ssid.trim();
    }

    this.systemService.updateSystem(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.warning('You must restart this device after saving for changes to take effect.');
          this.toastr.success('Saved network settings');
          this.savedChanges = true;
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(`Could not save. ${err.message}`);
          this.savedChanges = false;
        }
      });
  }

  showWifiPassword: boolean = false;
  toggleWifiPasswordVisibility() {
    this.showWifiPassword = !this.showWifiPassword;
  }

  public scanWifi() {
    this.scanning = true;
    this.http.get<{networks: WifiNetwork[]}>('/api/system/wifi/scan')
      .pipe(
        finalize(() => this.scanning = false)
      )
      .subscribe({
        next: (response) => {
          // Sort networks by signal strength (highest first)
          const networks = response.networks.sort((a, b) => b.rssi - a.rssi);

          // filter out poor Wi-Fi connections
          const poorNetworks = networks.filter(network => network.rssi >= -80);

          // Remove duplicate Network Names and show highest signal strength only
          const uniqueNetworks = poorNetworks.reduce((acc, network) => {
            if (!acc[network.ssid] || acc[network.ssid].rssi < network.rssi) {
              acc[network.ssid] = network;
            }
            return acc;
          }, {} as { [key: string]: WifiNetwork });

          // Convert the object back to an array
          const filteredNetworks = Object.values(uniqueNetworks);

          // Create dialog data
          const dialogData = filteredNetworks.map(n => ({
            label: n.ssid,
            rssi: n.rssi,
            value: n.ssid
          }));

          // Show dialog with network list
          this.dialogService.open('Select Wi-Fi Network', dialogData)
            .subscribe((selectedSsid: string) => {
              if (selectedSsid) {
                this.form.patchValue({ ssid: selectedSsid });
                this.form.markAsDirty();
              }
            });
        },
        error: (err) => {
          this.toastr.error('Failed to scan Wi-Fi networks');
        }
      });
  }

  public restart() {
    this.systemService.restart()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('Device restarted');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(`Could not restart. ${err.message}`);
        }
      });
  }
}
