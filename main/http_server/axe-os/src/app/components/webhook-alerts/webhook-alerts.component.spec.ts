import { ComponentFixture, TestBed } from '@angular/core/testing';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { of } from 'rxjs';

import { WebhookAlertsComponent } from './webhook-alerts.component';
import { SystemApiService } from '../../services/system.service';

describe('WebhookAlertsComponent', () => {
  let component: WebhookAlertsComponent;
  let fixture: ComponentFixture<WebhookAlertsComponent>;
  let systemService: jasmine.SpyObj<SystemApiService>;

  beforeEach(() => {
    systemService = jasmine.createSpyObj<SystemApiService>('SystemApiService', [
      'getWebhookAlertSettings',
      'updateWebhookAlertSettings',
      'testWebhookAlert',
    ]);
    systemService.getWebhookAlertSettings.and.returnValue(of({
      hasWebhook: true,
      watchdogEnabled: true,
      blockFoundEnabled: true,
      bestDiffEnabled: false,
    }));

    TestBed.configureTestingModule({
      imports: [WebhookAlertsComponent],
      providers: [
        provideHttpClient(),
        provideToastr(),
        { provide: SystemApiService, useValue: systemService },
      ]
    });
    fixture = TestBed.createComponent(WebhookAlertsComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('loads non-secret status and uses the secret sentinel', () => {
    expect(component.hasWebhook).toBeTrue();
    expect(component.form.controls['webhookUrl'].value).toBe('********');
    expect(component.form.controls['bestDiffEnabled'].value).toBeFalse();
  });

  it('explains each alert in plain language', () => {
    const text = fixture.nativeElement.textContent;

    expect(text).toContain('Get webhook alerts for specific miner events. Alerts are sent as JSON with a content field, compatible with Discord.');
    expect(text).toContain('Watchdog Reboot');
    expect(text).toContain('Alerts when the miner reboots after its firmware stops responding.');
    expect(text).toContain('Block Found');
    expect(text).toContain('Alerts when your miner finds a block.');
    expect(text).toContain('New Best Difficulty');
    expect(text).toContain('Alerts when your miner finds a new best difficulty.');
    expect(text).not.toContain('write-only');
  });

  it('rejects non-HTTPS webhook URLs', () => {
    component.form.controls['webhookUrl'].setValue('http://example.com/hook');
    expect(component.form.controls['webhookUrl'].invalid).toBeTrue();
  });

  it('rejects credentials and fragments in webhook URLs', () => {
    component.form.controls['webhookUrl'].setValue('https://user@example.com/hook');
    expect(component.form.controls['webhookUrl'].invalid).toBeTrue();
    component.form.controls['webhookUrl'].setValue('https://example.com/hook#secret');
    expect(component.form.controls['webhookUrl'].invalid).toBeTrue();
  });

  it('rejects malformed or out-of-range webhook ports', () => {
    for (const url of [
      'https://example.com:notaport/hook',
      'https://example.com:0/hook',
      'https://example.com:65536/hook',
    ]) {
      component.form.controls['webhookUrl'].setValue(url);
      expect(component.form.controls['webhookUrl'].invalid).withContext(url).toBeTrue();
    }
  });

  it('rejects non-ASCII webhook URLs before submission', () => {
    component.form.controls['webhookUrl'].setValue('https://example.com/hook-\u00e9');
    expect(component.form.controls['webhookUrl'].invalid).toBeTrue();
  });

  it('preserves the sentinel when saving switches', () => {
    systemService.updateWebhookAlertSettings.and.returnValue(of({
      hasWebhook: true,
      watchdogEnabled: false,
      blockFoundEnabled: true,
      bestDiffEnabled: false,
    }));
    component.form.controls['watchdogEnabled'].setValue(false);

    component.save();

    expect(systemService.updateWebhookAlertSettings).toHaveBeenCalledWith('', jasmine.objectContaining({
      webhookUrl: '********',
      watchdogEnabled: false,
    }));
    expect(component.hasWebhook).toBeTrue();
    expect(component.form.controls['webhookUrl'].value).toBe('********');
    expect(component.testDisabled).toBeFalse();
  });

  it('does not allow a test while settings are unsaved', () => {
    component.form.controls['bestDiffEnabled'].setValue(true);
    component.form.markAsDirty();
    expect(component.testDisabled).toBeTrue();
  });

  it('uses responsive sizing that keeps labels and actions readable', () => {
    const labels = Array.from(
      fixture.nativeElement.querySelectorAll('form > div > label')
    ) as HTMLElement[];
    const actionRow = fixture.nativeElement.querySelector('[data-testid="webhook-actions"]') as HTMLElement;
    const actionButtons = Array.from(actionRow.querySelectorAll('button')) as HTMLElement[];

    expect(labels.every(label => label.classList.contains('md:w-3/12'))).toBeTrue();
    expect(labels.every(label => label.classList.contains('xl:w-2/12'))).toBeTrue();
    expect(actionRow.classList.contains('flex-col')).toBeTrue();
    expect(actionRow.classList.contains('sm:flex-row')).toBeTrue();
    expect(actionButtons.every(button => button.classList.contains('w-full'))).toBeTrue();
    expect(actionButtons.every(button => button.classList.contains('sm:w-auto'))).toBeTrue();
    expect(actionButtons.every(button => button.classList.contains('whitespace-nowrap'))).toBeTrue();
  });
});
