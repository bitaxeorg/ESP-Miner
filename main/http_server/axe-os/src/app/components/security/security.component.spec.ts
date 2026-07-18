import { ComponentFixture, TestBed } from '@angular/core/testing';
import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { provideToastr } from 'ngx-toastr';
import { provideAnimations } from '@angular/platform-browser/animations';
import { SecurityComponent } from './security.component';

describe('SecurityComponent', () => {
  let component: SecurityComponent;
  let fixture: ComponentFixture<SecurityComponent>;
  let httpMock: HttpTestingController;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [SecurityComponent],
      providers: [provideHttpClient(), provideHttpClientTesting(), provideToastr(), provideAnimations()]
    });
    fixture = TestBed.createComponent(SecurityComponent);
    component = fixture.componentInstance;
    httpMock = TestBed.inject(HttpTestingController);
  });

  afterEach(() => httpMock.verify());

  // Trigger ngOnInit and satisfy the initial GET /api/system/auth request.
  function flushInit(enabled = 0, username = 'admin') {
    fixture.detectChanges();
    const req = httpMock.expectOne('/api/system/auth');
    expect(req.request.method).toBe('GET');
    req.flush({ enabled, username });
  }

  it('should create', () => {
    flushInit();
    expect(component).toBeTruthy();
  });

  it('reflects the enabled state returned by the API', () => {
    flushInit(1, 'operator');
    expect(component.authEnabled).toBeTrue();
    expect(component.username).toBe('operator');
  });

  it('is invalid when the passwords do not match', () => {
    flushInit();
    component.form.patchValue({ username: 'admin', newPassword: 'abcd', confirmPassword: 'abce' });
    expect(component.form.hasError('passwordMismatch')).toBeTrue();
    expect(component.form.invalid).toBeTrue();
  });

  it('is invalid when the password is too short', () => {
    flushInit();
    component.form.patchValue({ username: 'admin', newPassword: 'ab', confirmPassword: 'ab' });
    expect(component.form.get('newPassword')?.hasError('minlength')).toBeTrue();
  });

  it('save() posts the credentials and clears the password fields', () => {
    flushInit();
    component.form.patchValue({ username: 'admin', newPassword: 'longenough', confirmPassword: 'longenough' });
    component.save();
    const req = httpMock.expectOne('/api/system/auth');
    expect(req.request.method).toBe('POST');
    expect(req.request.body).toEqual({ username: 'admin', newPassword: 'longenough' });
    req.flush({ enabled: 1, message: 'Authentication enabled' });
    expect(component.authEnabled).toBeTrue();
    expect(component.form.get('newPassword')?.value).toBe('');
    expect(component.form.get('confirmPassword')?.value).toBe('');
  });

  it('save() issues no request when the form is invalid', () => {
    flushInit();
    component.form.patchValue({ username: 'admin', newPassword: 'x', confirmPassword: 'y' });
    component.save();
    httpMock.expectNone('/api/system/auth');
  });

  it('disable() clears the password when confirmed', () => {
    flushInit(1, 'admin');
    spyOn(window, 'confirm').and.returnValue(true);
    component.disable();
    const req = httpMock.expectOne('/api/system/auth');
    expect(req.request.method).toBe('POST');
    expect(req.request.body.newPassword).toBe('');
    req.flush({ enabled: 0, message: 'disabled' });
    expect(component.authEnabled).toBeFalse();
  });

  it('disable() aborts when the confirmation is declined', () => {
    flushInit(1, 'admin');
    spyOn(window, 'confirm').and.returnValue(false);
    component.disable();
    httpMock.expectNone('/api/system/auth');
  });
});
