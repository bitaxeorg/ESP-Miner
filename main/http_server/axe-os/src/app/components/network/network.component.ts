import { HttpErrorResponse, HttpEventType } from '@angular/common/http';
import { Component } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { map, Observable, shareReplay, startWith } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';

@Component({
  selector: 'app-network',
  templateUrl: './network.component.html',
  styleUrls: ['./network.component.scss']
})
export class NetworkComponent {

  public form!: FormGroup;

  public info$: Observable<any>;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private toastrService: ToastrService,
    private loadingService: LoadingService,
  ) {
    this.info$ = this.systemService.getInfo().pipe(shareReplay({refCount: true, bufferSize: 1}))

      this.info$.pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.form = this.fb.group({
          hostname: [info.hostname, [Validators.required]],
          ssid: [info.ssid, [Validators.required]],
          wifiPass: ['*****'],
        });
      });

  }

  public updateSystem() {

    const form = this.form.getRawValue();

    // Allow an empty wifi password
    form.wifiPass = form.wifiPass == null ? '' : form.wifiPass;

    if (form.wifiPass === '*****') {
      delete form.wifiPass;
    }

    this.systemService.updateSystem(undefined, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('Success!', 'Saved.');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save. ${err.message}`);
        }
      });
  }
}
