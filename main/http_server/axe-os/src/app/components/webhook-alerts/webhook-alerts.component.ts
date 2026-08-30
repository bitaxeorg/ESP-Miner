import { CommonModule } from '@angular/common';
import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnChanges, OnDestroy, OnInit, SimpleChanges } from '@angular/core';
import { AbstractControl, FormBuilder, FormGroup, ReactiveFormsModule, ValidationErrors } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { Subject, takeUntil } from 'rxjs';

import { CheckboxComponent } from '../checkbox/checkbox.component';
import { LoadingService } from '../../services/loading.service';
import { SystemApiService } from '../../services/system.service';
import { getHttpErrorMessage } from '../../utils/error-handler';

const WEBHOOK_SENTINEL = '********';

function webhookUrlValidator(control: AbstractControl): ValidationErrors | null {
  const value = control.value as string;
  if (!value || value === WEBHOOK_SENTINEL) {
    return null;
  }
  if (value.length > 512 || /\s/.test(value)) {
    return { webhookUrl: true };
  }
  try {
    const parsed = new URL(value);
    return parsed.protocol === 'https:' &&
      parsed.hostname.includes('.') &&
      !parsed.username &&
      !parsed.password &&
      !parsed.hash
      ? null
      : { webhookUrl: true };
  } catch {
    return { webhookUrl: true };
  }
}

@Component({
  selector: 'app-webhook-alerts',
  templateUrl: './webhook-alerts.component.html',
  standalone: true,
  imports: [CommonModule, ReactiveFormsModule, CheckboxComponent]
})
export class WebhookAlertsComponent implements OnInit, OnChanges, OnDestroy {
  @Input() uri = '';

  form: FormGroup;
  hasWebhook = false;
  private destroy$ = new Subject<void>();

  constructor(
    private fb: FormBuilder,
    private systemService: SystemApiService,
    private loadingService: LoadingService,
    private toastr: ToastrService,
  ) {
    this.form = this.fb.group({
      webhookUrl: ['', [webhookUrlValidator]],
      watchdogEnabled: [true],
      blockFoundEnabled: [true],
      bestDiffEnabled: [true],
    });
  }

  ngOnInit(): void {
    this.load();
  }

  ngOnChanges(changes: SimpleChanges): void {
    if (changes['uri'] && !changes['uri'].firstChange) {
      this.load();
    }
  }

  ngOnDestroy(): void {
    this.destroy$.next();
    this.destroy$.complete();
  }

  load(): void {
    this.systemService.getWebhookAlertSettings(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete(), takeUntil(this.destroy$))
      .subscribe({
        next: settings => {
          this.hasWebhook = settings.hasWebhook;
          this.form.reset({
            webhookUrl: settings.hasWebhook ? WEBHOOK_SENTINEL : '',
            watchdogEnabled: settings.watchdogEnabled,
            blockFoundEnabled: settings.blockFoundEnabled,
            bestDiffEnabled: settings.bestDiffEnabled,
          });
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(`Could not load webhook alerts. ${getHttpErrorMessage(err, this.uri)}`);
        }
      });
  }

  save(): void {
    if (this.form.invalid) {
      return;
    }

    this.systemService.updateWebhookAlertSettings(this.uri, this.form.getRawValue())
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: settings => {
          this.hasWebhook = settings.hasWebhook;
          this.form.reset({
            webhookUrl: settings.hasWebhook ? WEBHOOK_SENTINEL : '',
            watchdogEnabled: settings.watchdogEnabled,
            blockFoundEnabled: settings.blockFoundEnabled,
            bestDiffEnabled: settings.bestDiffEnabled,
          });
          this.toastr.success('Saved webhook alert settings');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(`Could not save webhook alerts. ${getHttpErrorMessage(err, this.uri)}`);
        }
      });
  }

  clearWebhook(): void {
    const update = {
      ...this.form.getRawValue(),
      webhookUrl: '',
    };
    this.systemService.updateWebhookAlertSettings(this.uri, update)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: settings => {
          this.hasWebhook = settings.hasWebhook;
          this.form.patchValue({ webhookUrl: '' });
          this.form.markAsPristine();
          this.toastr.success('Cleared webhook');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(`Could not clear webhook. ${getHttpErrorMessage(err, this.uri)}`);
        }
      });
  }

  testWebhook(): void {
    this.systemService.testWebhookAlert(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => this.toastr.success('Webhook test delivered'),
        error: (err: HttpErrorResponse) => {
          this.toastr.error(`Webhook test failed. ${getHttpErrorMessage(err, this.uri)}`);
        }
      });
  }

  get testDisabled(): boolean {
    return !this.hasWebhook || this.form.dirty;
  }
}
