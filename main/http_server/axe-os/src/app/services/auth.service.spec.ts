import { TestBed } from '@angular/core/testing';
import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { AuthService } from './auth.service';

describe('AuthService', () => {
  let service: AuthService;
  let httpMock: HttpTestingController;

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [AuthService, provideHttpClient(), provideHttpClientTesting()]
    });
    service = TestBed.inject(AuthService);
    httpMock = TestBed.inject(HttpTestingController);
  });

  afterEach(() => httpMock.verify());

  it('should be created', () => {
    expect(service).toBeTruthy();
  });

  it('getAuthInfo() GETs /api/system/auth', () => {
    let result: any;
    service.getAuthInfo().subscribe(r => (result = r));
    const req = httpMock.expectOne('/api/system/auth');
    expect(req.request.method).toBe('GET');
    req.flush({ enabled: 1, username: 'admin' });
    expect(result.enabled).toBe(1);
    expect(result.username).toBe('admin');
  });

  it('setCredentials() POSTs username and newPassword', () => {
    let result: any;
    service.setCredentials('operator', 's3cret').subscribe(r => (result = r));
    const req = httpMock.expectOne('/api/system/auth');
    expect(req.request.method).toBe('POST');
    expect(req.request.body).toEqual({ username: 'operator', newPassword: 's3cret' });
    req.flush({ enabled: 1, message: 'ok' });
    expect(result.enabled).toBe(1);
  });

  it('setCredentials() with an empty password disables auth', () => {
    let result: any;
    service.setCredentials('admin', '').subscribe(r => (result = r));
    const req = httpMock.expectOne('/api/system/auth');
    expect(req.request.method).toBe('POST');
    expect(req.request.body).toEqual({ username: 'admin', newPassword: '' });
    req.flush({ enabled: 0, message: 'disabled' });
    expect(result.enabled).toBe(0);
  });
});
