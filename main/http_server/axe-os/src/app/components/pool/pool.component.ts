import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';

type PoolType = 'stratum' | 'fallbackStratum';

@Component({
  selector: 'app-pool',
  templateUrl: './pool.component.html',
  styleUrls: ['./pool.component.scss']
})
export class PoolComponent implements OnInit {
  public form!: FormGroup;
  public savedChanges: boolean = false;

  public pools: PoolType[] = ['stratum', 'fallbackStratum'];
  public showPassword = {'stratum': false, 'fallbackStratum': false};
  public showAdvancedOptions = {'stratum': false, 'fallbackStratum': false};

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private loadingService: LoadingService
  ) {}

  ngOnInit(): void {
    this.systemService.getInfo(this.uri)
      .pipe(
        this.loadingService.lockUIUntilComplete()
      )
      .subscribe(info => {
        this.form = this.fb.group({
          stratumURL: [info.stratumURL, [
            Validators.required,
            Validators.pattern(/^(?!.*stratum\+tcp:\/\/).*$/),
            Validators.pattern(/^[^:]*$/),
          ]],
          stratumPort: [info.stratumPort, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(0),
            Validators.max(65535)
          ]],
          stratumExtranonceSubscribe: [info.stratumExtranonceSubscribe == 1, [Validators.required]],
          stratumSuggestedDifficulty: [info.stratumSuggestedDifficulty, [Validators.required]],
          stratumUser: [info.stratumUser, [Validators.required]],
          stratumPassword: ['*****', [Validators.required]],

          fallbackStratumURL: [info.fallbackStratumURL, [
            Validators.pattern(/^(?!.*stratum\+tcp:\/\/).*$/),
          ]],
          fallbackStratumPort: [info.fallbackStratumPort, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(0),
            Validators.max(65535)
          ]],
          fallbackStratumExtranonceSubscribe: [info.fallbackStratumExtranonceSubscribe == 1, [Validators.required]],
          fallbackStratumSuggestedDifficulty: [info.fallbackStratumSuggestedDifficulty, [Validators.required]],
          fallbackStratumUser: [info.fallbackStratumUser, [Validators.required]],
          fallbackStratumPassword: ['*****', [Validators.required]]
        });
      });
  }

  public updateSystem() {
    const form = this.form.getRawValue();

    if (form.stratumPassword === '*****') {
      delete form.stratumPassword;
    }
    if (form.fallbackStratumPassword === '*****') {
      delete form.fallbackStratumPassword;
    }

    this.systemService.updateSystem(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Saved pool settings for ${this.uri}` : 'Saved pool settings';
          this.toastr.warning('You must restart this device after saving for changes to take effect.');
          this.toastr.success(successMessage);
          this.savedChanges = true;
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Could not save pool settings for ${this.uri}. ${err.message}` : `Could not save pool settings. ${err.message}`;
          this.toastr.error(errorMessage);
          this.savedChanges = false;
        }
      });
  }

  public restart() {
    this.systemService.restart(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Device at ${this.uri} restarted` : 'Device restarted';
          this.toastr.success(successMessage);
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Failed to restart device at ${this.uri}. ${err.message}` : `Failed to restart device. ${err.message}`;
          this.toastr.error(errorMessage);
        }
      });
  }
}
