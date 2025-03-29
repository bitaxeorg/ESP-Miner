import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { startWith } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';

@Component({
  selector: 'app-influx',
  templateUrl: './influxdb.component.html',
  styleUrls: ['./influxdb.component.scss']
})
export class InfluxdbComponent implements OnInit {

  public form!: FormGroup;

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private toastrService: ToastrService,
    private loadingService: LoadingService
  ) {

  }
  ngOnInit(): void {
    this.systemService.getInfluxInfo(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.form = this.fb.group({
          influxURL: [info.influxURL, [
            Validators.required,
            Validators.pattern(/^http:\/\/.*[^:]*$/), // http:// without port
          ]],
          influxPort: [info.influxPort, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(0),
            Validators.max(65353)
          ]],
          influxToken: ['password', [Validators.required]],
          influxBucket: [info.influxBucket, [Validators.required]],
          influxOrg: [info.influxOrg, [Validators.required]],
          influxPrefix: [info.influxPrefix, [Validators.required]],
          influxEnable: [info.influxEnable == 1]
        });
      });
  }

  public updateSystem() {

    const form = this.form.getRawValue();

    if (form.influxToken === 'password') {
      delete form.influxToken;
    }

    this.systemService.updateInflux(this.uri, form)
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
