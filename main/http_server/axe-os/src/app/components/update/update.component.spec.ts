import { ComponentFixture, TestBed } from '@angular/core/testing';

import { UpdateComponent } from './update.component';
import { ModalComponent } from '../modal/modal.component';
import { CheckboxComponent } from '../checkbox/checkbox.component';
import { FormsModule } from '@angular/forms';
import { ProgressbarComponent } from '../progressbar/progressbar.component';
import { provideHttpClient, HttpErrorResponse } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { getHttpErrorMessage } from 'src/app/utils/error-handler';
import { of, throwError } from 'rxjs';
import { SystemApiService } from 'src/app/services/system.service';
import { GithubRelease, GithubUpdateService } from 'src/app/services/github-update.service';

describe('UpdateComponent', () => {
  let component: UpdateComponent;
  let fixture: ComponentFixture<UpdateComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [UpdateComponent, ModalComponent],
      imports: [CheckboxComponent, ProgressbarComponent, FormsModule],
      providers: [provideHttpClient(), provideToastr()]
    });
    fixture = TestBed.createComponent(UpdateComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
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
