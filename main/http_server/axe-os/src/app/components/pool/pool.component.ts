import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators, ValidatorFn, ValidationErrors, AbstractControl } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';

@Component({
  selector: 'app-pool',
  templateUrl: './pool.component.html',
  styleUrls: ['./pool.component.scss']
})
export class PoolComponent implements OnInit {
  public form!: FormGroup;
  public savedChanges: boolean = false;

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
            Validators.max(65353)
          ]],
          stratumTLS: [info.stratumTLS || 0],
          stratumCert: [info.stratumCert],
          fallbackStratumURL: [info.fallbackStratumURL, [
            Validators.pattern(/^(?!.*stratum\+tcp:\/\/).*$/),
          ]],
          fallbackStratumPort: [info.fallbackStratumPort, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(0),
            Validators.max(65353)
          ]],
          fallbackStratumTLS: [info.fallbackStratumTLS || 0],
          fallbackStratumCert: [info.fallbackStratumCert],
          stratumUser: [info.stratumUser, [Validators.required]],
          stratumPassword: ['*****', [Validators.required]],
          fallbackStratumUser: [info.fallbackStratumUser, [Validators.required]],
          fallbackStratumPassword: ['password', [Validators.required]]
        });

        // Add conditional validation for primary stratumCert
        this.form.get('stratumTLS')?.valueChanges.subscribe(value => {
          const certControl = this.form.get('stratumCert');
          if (value === 2) { // Only validate certificate when "Custom CA" (value 2) is selected
            certControl?.setValidators([
              Validators.required,
              this.pemCertificateValidator()
            ]);
          } else {
            certControl?.clearValidators();
          }
          certControl?.updateValueAndValidity();
        });

        // Add conditional validation for fallback stratumCert
        this.form.get('fallbackStratumTLS')?.valueChanges.subscribe(value => {
          const certControl = this.form.get('fallbackStratumCert');
          if (value === 2) { // Only validate certificate when "Custom CA" (value 2) is selected
            certControl?.setValidators([
              Validators.required,
              this.pemCertificateValidator()
            ]);
          } else {
            certControl?.clearValidators();
          }
          certControl?.updateValueAndValidity();
        });

        // Trigger initial validation
        this.form.get('stratumTLS')?.updateValueAndValidity();
        this.form.get('fallbackStratumTLS')?.updateValueAndValidity();
      });
  }

  public updateSystem() {
    const form = this.form.getRawValue();

    if (form.stratumPassword === '*****') {
      delete form.stratumPassword;
    }

    this.systemService.updateSystem(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Saved pool settings for ${this.uri}` : 'Saved pool settings';
          this.toastr.success(successMessage, 'Success!');
          this.savedChanges = true;
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Could not save pool settings for ${this.uri}. ${err.message}` : `Could not save pool settings. ${err.message}`;
          this.toastr.error(errorMessage, 'Error');
          this.savedChanges = false;
        }
      });
  }

  showStratumPassword: boolean = false;
  toggleStratumPasswordVisibility() {
    this.showStratumPassword = !this.showStratumPassword;
  }

  showFallbackStratumPassword: boolean = false;
  toggleFallbackStratumPasswordVisibility() {
    this.showFallbackStratumPassword = !this.showFallbackStratumPassword;
  }

  public restart() {
    this.systemService.restart(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Bitaxe at ${this.uri} restarted` : 'Bitaxe restarted';
          this.toastr.success(successMessage, 'Success');
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Failed to restart device at ${this.uri}. ${err.message}` : `Failed to restart device. ${err.message}`;
          this.toastr.error(errorMessage, 'Error');
        }
      });
  }

  /**
   * Handles certificate file selection and reads the file content
   * @param event File selection event
   * @param formControlName Form control name ('stratumCert' or 'fallbackStratumCert')
   */
  onCertFileSelected(event: Event, formControlName: 'stratumCert' | 'fallbackStratumCert'): void {
    const fileInput = event.target as HTMLInputElement;
    
    if (fileInput.files && fileInput.files.length > 0) {
      const file = fileInput.files[0];
      const reader = new FileReader();
      
      reader.onload = () => {
        const fileContent = reader.result as string;
        // Update the corresponding certificate field in the form
        this.form.get(formControlName)?.setValue(fileContent);
        this.form.get(formControlName)?.markAsDirty();
        
        // Reset file input so the same file can be selected again
        fileInput.value = '';
      };
      
      reader.onerror = () => {
        // Error handling when reading the certificate file
        this.toastr.error('Failed to read certificate file', 'Error');
        fileInput.value = '';
      };
      
      // Read the file as text
      reader.readAsText(file);
    }
  }

  private pemCertificateValidator(): ValidatorFn {
    return (control: AbstractControl): ValidationErrors | null => {
      if (!control.value?.trim()) return null;
      
      const pemRegex = /^-----BEGIN CERTIFICATE-----\s*([\s\S]*?)\s*-----END CERTIFICATE-----$/;
      return pemRegex.test(control.value?.trim()) ? null : { invalidCertificate: true };
    };
  }
}
