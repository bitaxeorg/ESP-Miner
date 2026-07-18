import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormBuilder, FormGroup, Validators, ReactiveFormsModule, AbstractControl, ValidationErrors } from '@angular/forms';
import { HttpErrorResponse } from '@angular/common/http';
import { ToastrService } from 'ngx-toastr';
import { finalize } from 'rxjs';
import { AuthService } from '../../services/auth.service';

// Keep these in sync with the firmware limits (components/http_auth/http_auth.h).
const USERNAME_MAX = 32;
const PASSWORD_MAX = 64;
const PASSWORD_MIN = 4;

// Cross-field validator: newPassword must equal confirmPassword.
function passwordsMatch(group: AbstractControl): ValidationErrors | null {
  const pw = group.get('newPassword')?.value ?? '';
  const confirm = group.get('confirmPassword')?.value ?? '';
  return pw === confirm ? null : { passwordMismatch: true };
}

@Component({
  selector: 'app-security',
  templateUrl: './security.component.html',
  standalone: true,
  imports: [CommonModule, ReactiveFormsModule]
})
export class SecurityComponent implements OnInit {
  public form: FormGroup;
  public authEnabled = false;
  public username = 'admin';
  public loading = true;
  public saving = false;

  public readonly usernameMax = USERNAME_MAX;
  public readonly passwordMax = PASSWORD_MAX;
  public readonly passwordMin = PASSWORD_MIN;

  constructor(
    private fb: FormBuilder,
    private authService: AuthService,
    private toastr: ToastrService
  ) {
    this.form = this.fb.group({
      username: ['admin', [Validators.required, Validators.maxLength(USERNAME_MAX)]],
      newPassword: ['', [Validators.required, Validators.minLength(PASSWORD_MIN), Validators.maxLength(PASSWORD_MAX)]],
      confirmPassword: ['', [Validators.required]]
    }, { validators: passwordsMatch });
  }

  ngOnInit(): void {
    this.authService.getAuthInfo()
      .pipe(finalize(() => this.loading = false))
      .subscribe({
        next: info => {
          this.authEnabled = info.enabled === 1;
          this.username = info.username || 'admin';
          this.form.patchValue({ username: this.username });
        },
        error: () => {
          // Non-fatal: leave defaults. The panel still lets the user set a password.
        }
      });
  }

  public save(): void {
    if (this.form.invalid) {
      this.form.markAllAsTouched();
      return;
    }
    const { username, newPassword } = this.form.value;
    this.saving = true;
    this.authService.setCredentials(username, newPassword)
      .pipe(finalize(() => this.saving = false))
      .subscribe({
        next: res => {
          this.authEnabled = res.enabled === 1;
          this.username = username;
          this.form.patchValue({ newPassword: '', confirmPassword: '' });
          this.form.markAsPristine();
          this.toastr.success(res.message || 'Authentication updated', 'Success');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(err?.error?.message || err?.message || 'Failed to update authentication', 'Error');
        }
      });
  }

  public disable(): void {
    if (!confirm('Disable web authentication? Anyone on the network will be able to access and change this device.')) {
      return;
    }
    const username = this.form.get('username')?.value || this.username || 'admin';
    this.saving = true;
    // Empty password clears/disables authentication.
    this.authService.setCredentials(username, '')
      .pipe(finalize(() => this.saving = false))
      .subscribe({
        next: res => {
          this.authEnabled = res.enabled === 1;
          this.form.patchValue({ newPassword: '', confirmPassword: '' });
          this.form.markAsPristine();
          this.toastr.success(res.message || 'Authentication disabled', 'Success');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(err?.error?.message || err?.message || 'Failed to disable authentication', 'Error');
        }
      });
  }
}
