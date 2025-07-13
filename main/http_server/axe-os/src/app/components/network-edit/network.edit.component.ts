import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { finalize } from 'rxjs/operators';
import { BehaviorSubject, Observable } from 'rxjs';
import { DialogService } from 'src/app/services/dialog.service';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';

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
  public isInCaptivePortalMode: boolean = false;

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
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
        // Check if we're in captive portal mode (no SSID configured)
        this.isInCaptivePortalMode = !info.ssid || info.ssid.trim() === '';

        // Generate suggested hostname from MAC address (bitaxe-xxxx pattern)
        const suggestedHostname = this.generateSuggestedHostname(info.macAddr);
        const hostname = this.isInCaptivePortalMode ? suggestedHostname : info.hostname;

        this.form = this.fb.group({
          hostname: [hostname, [Validators.required]],
          ssid: [info.ssid, [Validators.required]],
          wifiPass: ['*****'],
          apiSecret: [info.apiSecret || '', [Validators.minLength(12), Validators.maxLength(32)]],
        });
        this.formSubject.next(this.form);
      });
  }

  private generateSuggestedHostname(macAddr: string): string {
    if (!macAddr) return 'bitaxe';

    // Extract last 4 characters (2 bytes) from MAC address
    // MAC format is XX:XX:XX:XX:XX:XX, so get last 2 pairs
    const macParts = macAddr.split(':');
    if (macParts.length >= 6) {
      const lastTwoBytes = macParts[4] + macParts[5];
      return `bitaxe-${lastTwoBytes.toLowerCase()}`;
    }

    return 'bitaxe';
  }

  public generateApiSecret(): void {
    const characters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    let result = '';
    for (let i = 0; i < 24; i++) {
      result += characters.charAt(Math.floor(Math.random() * characters.length));
    }
    this.form.patchValue({ apiSecret: result });
    this.form.markAsDirty();
  }

  public getHostnameUrl(): string {
    const hostname = this.form?.get('hostname')?.value || 'bitaxe';
    return `http://${hostname}.local`;
  }


  public updateSystem() {

    const form = this.form.getRawValue();

    // Allow an empty wifi password
    form.wifiPass = form.wifiPass == null ? '' : form.wifiPass;

    if (form.wifiPass === '*****') {
      delete form.wifiPass;
    }

    // Trim SSID to remove any leading/trailing whitespace
    if (form.ssid) {
      form.ssid = form.ssid.trim();
    }

    // Handle API secret - remove if empty
    if (form.apiSecret && form.apiSecret.trim() === '') {
      form.apiSecret = '';
    }

    this.systemService.updateSystem(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.warning('You must restart this device after saving for changes to take effect', 'Warning');
          this.toastr.success('Success!', 'Saved network settings');
          this.savedChanges = true;
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save. ${err.message}`);
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

          // filter out poor wifi connections
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
          this.toastr.error('Failed to scan Wi-Fi networks', 'Error');
        }
      });
  }

  public restart() {
    this.systemService.restart()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('Success!', 'Bitaxe restarted');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error', `Could not restart. ${err.message}`);
        }
      });
  }
}
