import { HttpErrorResponse } from '@angular/common/http';
import { Component, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { shareReplay } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';
import { ISystemInfo } from 'src/models/ISystemInfo';

@Component({
  selector: 'app-influxdb',
  templateUrl: './influxdb.component.html',
  styleUrls: ['./influxdb.component.scss']
})
export class InfluxdbComponent implements OnInit {
  public form!: FormGroup;
  public info$: any;
  public savedChanges = false;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private loadingService: LoadingService
  ) {
    this.info$ = this.systemService.getInfo().pipe(shareReplay({refCount: true, bufferSize: 1}));
  }

  ngOnInit() {
    this.info$.pipe(this.loadingService.lockUIUntilComplete())
      .subscribe((info: ISystemInfo) => {
        // Initialize form with existing values or defaults
        this.form = this.fb.group({
          influxEnabled: [info.influxEnabled ? true : false],  // Convert to boolean explicitly
          influxHost: [info.influxHost || '', [Validators.required, Validators.pattern(/^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$/)]],
          influxPort: [info.influxPort || '8086', [Validators.required, Validators.pattern(/^\d+$/), Validators.min(1), Validators.max(65535)]],
          influxToken: ['*****', [Validators.required]],
          influxOrg: [info.influxOrg || ''],
          influxBucket: [info.influxBucket || '']
        });

        // Mark form as pristine after initialization
        this.form.markAsPristine();
      });
  }

  public updateSystem() {
    const formValue = this.form.getRawValue();

    const update = {
      influxEnabled: formValue.influxEnabled ? 1 : 0,
      influxHost: formValue.influxHost,
      influxPort: formValue.influxPort,
      // Only send token if it's been changed from the default asterisks
      ...(formValue.influxToken !== '*****' && { influxToken: formValue.influxToken }),
      influxOrg: formValue.influxOrg,
      influxBucket: formValue.influxBucket
    };

    this.systemService.updateSystem(undefined, update)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.savedChanges = true;
          this.form.markAsPristine();
          // Reset token field to asterisks after successful save
          this.form.patchValue({ influxToken: '*****' });
          this.toastr.success('Success!', 'InfluxDB settings saved.');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save InfluxDB settings. ${err.message}`);
        }
      });
  }

  public restart() {
    this.systemService.restart()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('Success!', 'System is restarting...');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not restart system. ${err.message}`);
        }
      });
  }
}