import { ComponentFixture, TestBed } from '@angular/core/testing';

import { UpdateComponent } from './update.component';
import { ModalComponent } from '../modal/modal.component';
import { CheckboxComponent } from '../checkbox/checkbox.component';
import { FormsModule } from '@angular/forms';
import { ProgressbarComponent } from '../progressbar/progressbar.component';
import { provideHttpClient, HttpErrorResponse } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { getHttpErrorMessage } from 'src/app/utils/error-handler';
import { BehaviorSubject, of, throwError } from 'rxjs';
import { LiveDataService } from 'src/app/services/live-data.service';
import { SystemInfo } from 'src/app/generated/models';
import { SystemApiService } from 'src/app/services/system.service';
import { GithubRelease, GithubUpdateService } from 'src/app/services/github-update.service';

describe('UpdateComponent', () => {
  let component: UpdateComponent;
  let fixture: ComponentFixture<UpdateComponent>;
  let info: BehaviorSubject<SystemInfo>;

  beforeEach(() => {
    info = new BehaviorSubject<SystemInfo>({ version: 'test', otaButtonRequired: 1, otaUnlockRemaining: 0 } as SystemInfo);
    TestBed.configureTestingModule({
      imports: [CheckboxComponent, ProgressbarComponent, FormsModule, UpdateComponent, ModalComponent],
      providers: [provideHttpClient(), provideToastr(),
        { provide: LiveDataService, useValue: { info$: info.asObservable(), connected$: of(false) } }]
    });
    fixture = TestBed.createComponent(UpdateComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  describe('OTA protection', () => {
    it('blocks firmware and custom web UI uploads while locked', () => {
      info.next({ ...info.value, useCustomWWW: 1 });
      fixture.detectChanges();
      const service = TestBed.inject(SystemApiService);
      const firmware = spyOn(service, 'performOTAUpdate');
      const website = spyOn(service, 'performWWWOTAUpdate');
      component.otaUpdate(new File(['firmware'], 'esp-miner.bin'));
      component.otaWWWUpdate(new File(['website'], 'www.bin'));
      expect(firmware).not.toHaveBeenCalled();
      expect(website).not.toHaveBeenCalled();
      expect(fixture.nativeElement.querySelector('input[type="file"]').disabled).toBeTrue();
      expect(fixture.nativeElement.querySelector('#otaButtonRequired').disabled).toBeTrue();
      const browse: HTMLButtonElement = fixture.nativeElement.querySelector('button[aria-label="Browse firmware"]');
      const fileInput: HTMLInputElement = fixture.nativeElement.querySelector('input[type="file"]');
      const openPicker = spyOn(fileInput, 'click');
      expect(browse.disabled).toBeTrue();
      expect(fixture.nativeElement.querySelector('button[aria-label="Browse custom web UI"]').disabled).toBeTrue();
      browse.click();
      expect(openPicker).not.toHaveBeenCalled();
    });

    it('unlocks controls on a button press and relocks when the window expires', () => {
      info.next({ ...info.value, useCustomWWW: 1, otaUnlockRemaining: 120 });
      fixture.detectChanges();
      expect(component.otaLocked).toBeFalse();
      expect(fixture.nativeElement.textContent).toContain('Updates are unlocked for 120 seconds');
      expect(fixture.nativeElement.querySelector('input[type="file"]').disabled).toBeFalse();
      expect(fixture.nativeElement.querySelector('#otaButtonRequired').disabled).toBeFalse();
      const browse: HTMLButtonElement = fixture.nativeElement.querySelector('button[aria-label="Browse firmware"]');
      const fileInput: HTMLInputElement = fixture.nativeElement.querySelector('input[type="file"]');
      const openPicker = spyOn(fileInput, 'click');
      expect(browse.disabled).toBeFalse();
      expect(fixture.nativeElement.querySelector('button[aria-label="Browse custom web UI"]').disabled).toBeFalse();
      browse.click();
      expect(openPicker).toHaveBeenCalledTimes(1);
      info.next({ ...info.value, otaUnlockRemaining: 0 });
      fixture.detectChanges();
      expect(component.otaLocked).toBeTrue();
      expect(fixture.nativeElement.querySelector('input[type="file"]').disabled).toBeTrue();
      expect(browse.disabled).toBeTrue();
      expect(fixture.nativeElement.querySelector('button[aria-label="Browse custom web UI"]').disabled).toBeTrue();
    });

    it('allows remote uploads when button protection is disabled', () => {
      info.next({ ...info.value, otaButtonRequired: 0 });
      const upload = spyOn(TestBed.inject(SystemApiService), 'performOTAUpdate').and.returnValue(of());
      component.otaUpdate(new File(['firmware'], 'esp-miner.bin'));
      expect(upload).toHaveBeenCalled();
      expect(component.otaLocked).toBeFalse();
    });

    it('does not activate installed firmware while locked', () => {
      const activate = spyOn(TestBed.inject(SystemApiService), 'switchBootPartition');
      component.switchPartition('factory');
      expect(activate).not.toHaveBeenCalled();
    });

    it('shows the backend error if the unlock expires before an upload starts', () => {
      info.next({ ...info.value, otaUnlockRemaining: 1 });
      spyOn(TestBed.inject(SystemApiService), 'performOTAUpdate').and.returnValue(
        throwError(() => new HttpErrorResponse({ status: 403, error: { message: 'Press the BOOT button to unlock OTA for 120 seconds' } }))
      );
      component.otaUpdate(new File(['firmware'], 'esp-miner.bin'));
      expect(component.updateStatus).toBe('error');
      expect(component.updateMessage).toContain('Press the BOOT button');
    });

    it('preserves the displayed protection setting when disabling is rejected', () => {
      const update = spyOn(TestBed.inject(SystemApiService), 'updateSystem').and.returnValue(
        throwError(() => new HttpErrorResponse({ status: 403 }))
      );
      info.next({ ...info.value, otaUnlockRemaining: 1 });
      fixture.detectChanges();
      const checkbox: HTMLInputElement = fixture.nativeElement.querySelector('#otaButtonRequired');
      checkbox.checked = false;
      checkbox.dispatchEvent(new Event('change'));
      expect(update).toHaveBeenCalledWith('', { otaButtonRequired: 0 });
      expect(checkbox.checked).toBeTrue();
    });
  });

  describe('getHttpErrorMessage', () => {
    it('should format HttpErrorResponse with status 0 as network error', () => {
      const err = new HttpErrorResponse({ status: 0, statusText: 'Unknown Error' });
      const msg = getHttpErrorMessage(err);
      expect(msg).toBe('Network error or connection lost. The device may have restarted or disconnected.');
    });

    it('should format HttpErrorResponse with string error body', () => {
      const err = new HttpErrorResponse({ status: 500, error: 'Write Error' });
      const msg = getHttpErrorMessage(err);
      expect(msg).toBe('Write Error');
    });

    it('should format HttpErrorResponse with JSON error body containing message', () => {
      const err = new HttpErrorResponse({ status: 500, error: { message: 'Out of flash memory' } });
      const msg = getHttpErrorMessage(err);
      expect(msg).toBe('Out of flash memory');
    });

    it('should format HttpErrorResponse with ProgressEvent error body', () => {
      const progressEvent = new ProgressEvent('error');
      const err = new HttpErrorResponse({ status: 500, error: progressEvent, statusText: 'Server Error' });
      const msg = getHttpErrorMessage(err);
      expect(msg).toBe('Upload failed: network error or connection closed.');
    });

    it('should format generic Error object message', () => {
      const err = new Error('Disk full');
      const msg = getHttpErrorMessage(err);
      expect(msg).toBe('Disk full');
    });

    it('should return string directly', () => {
      const msg = getHttpErrorMessage('Custom direct string error');
      expect(msg).toBe('Custom direct string error');
    });

    it('should return fallback message for null/undefined/other types', () => {
      expect(getHttpErrorMessage(null)).toBe('An unknown error occurred.');
      expect(getHttpErrorMessage(undefined)).toBe('An unknown error occurred.');
      expect(getHttpErrorMessage(123)).toBe('An unknown error occurred.');
    });

    it('should append device URI if provided', () => {
      const err = new HttpErrorResponse({ status: 500, error: 'Write Error' });
      const msg = getHttpErrorMessage(err, '192.168.1.10');
      expect(msg).toBe('Write Error (Device: 192.168.1.10)');
    });
  });

  describe('verifyFirmware', () => {
    const sha = 'ab'.repeat(32);
    let githubService: GithubUpdateService;

    const release = (digest?: string | null): GithubRelease => ({
      id: 1,
      tag_name: 'v2.13.0',
      name: 'v2.13.0',
      html_url: 'https://github.com/bitaxeorg/esp-miner/releases/tag/v2.13.0',
      prerelease: false,
      assets: [{ name: 'esp-miner.bin', browser_download_url: '', digest }]
    });

    beforeEach(() => {
      spyOn(TestBed.inject(SystemApiService), 'getFirmwareChecksum').and.returnValue(
        of({ partition: 'ota_0', version: 'v2.13.0', size: 1024, sha256: sha })
      );
      githubService = TestBed.inject(GithubUpdateService);
    });

    it('should report a match when the release digest equals the device checksum', () => {
      const spy = spyOn(githubService, 'getReleaseByTag').and.returnValue(of(release(`sha256:${sha.toUpperCase()}`)));
      component.verifyFirmware();
      expect(spy).toHaveBeenCalledWith('v2.13.0');
      expect(component.verifyStatus).toBe('match');
      expect(component.releaseChecksum).toBe(sha);
    });

    it('should report a mismatch when the digests differ', () => {
      spyOn(githubService, 'getReleaseByTag').and.returnValue(of(release(`sha256:${'cd'.repeat(32)}`)));
      component.verifyFirmware();
      expect(component.verifyStatus).toBe('mismatch');
    });

    it('should report no-digest when the release asset has no digest', () => {
      spyOn(githubService, 'getReleaseByTag').and.returnValue(of(release(null)));
      component.verifyFirmware();
      expect(component.verifyStatus).toBe('no-digest');
    });

    it('should report no-release when the tag does not exist', () => {
      spyOn(githubService, 'getReleaseByTag').and.returnValue(throwError(() => new HttpErrorResponse({ status: 404 })));
      component.verifyFirmware();
      expect(component.verifyStatus).toBe('no-release');
    });

    it('should report an error for other failures', () => {
      spyOn(githubService, 'getReleaseByTag').and.returnValue(throwError(() => new HttpErrorResponse({ status: 500 })));
      component.verifyFirmware();
      expect(component.verifyStatus).toBe('error');
    });
  });
});
