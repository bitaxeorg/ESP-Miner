import { TestBed } from '@angular/core/testing';
import { HttpClient, provideHttpClient, withInterceptors } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { AuthService } from './auth.service';
import { authInterceptor } from './auth.interceptor';

describe('AuthService & authInterceptor', () => {
  let authService: AuthService;
  let http: HttpClient;
  let httpMock: HttpTestingController;

  beforeEach(() => {
    localStorage.removeItem('esp_miner_auth_tokens');
    TestBed.configureTestingModule({
      providers: [
        provideHttpClient(withInterceptors([authInterceptor])),
        provideHttpClientTesting()
      ]
    });
    authService = TestBed.inject(AuthService);
    http = TestBed.inject(HttpClient);
    httpMock = TestBed.inject(HttpTestingController);
  });

  afterEach(() => {
    httpMock.verify();
    localStorage.removeItem('esp_miner_auth_tokens');
  });

  it('encodes password-only prompt as admin:<password> Basic token', () => {
    expect(authService.encodeBasicToken('secret')).toBe(window.btoa('admin:secret'));
  });

  it('does not attach auth or X-Requested-With headers to external non-API URLs', () => {
    authService.setCredentials('api.github.com', 'dGVzdA==');

    http.get('https://api.github.com/repos/bitaxeorg/esp-miner/releases').subscribe();

    const req = httpMock.expectOne('https://api.github.com/repos/bitaxeorg/esp-miner/releases');
    expect(req.request.headers.has('Authorization')).toBeFalse();
    expect(req.request.headers.has('X-Requested-With')).toBeFalse();
    req.flush([]);
  });

  it('re-prompts with Invalid password when retried request returns 401, then succeeds', () => {
    let result: unknown;
    http.post('/api/system/restart', {}).subscribe((res) => {
      result = res;
    });

    const firstReq = httpMock.expectOne('/api/system/restart');
    expect(firstReq.request.headers.get('X-Requested-With')).toBe('XMLHttpRequest');
    firstReq.flush({ authRequired: 1 }, { status: 401, statusText: 'Unauthorized' });

    // Submit wrong password first
    authService.submitPrompt('wrong-pass');

    const secondReq = httpMock.expectOne('/api/system/restart');
    expect(secondReq.request.headers.get('Authorization')).toBe(`Basic ${window.btoa('admin:wrong-pass')}`);
    secondReq.flush({ authRequired: 1 }, { status: 401, statusText: 'Unauthorized' });

    // Submit correct password on re-prompt
    authService.submitPrompt('correct-pass');

    const thirdReq = httpMock.expectOne('/api/system/restart');
    expect(thirdReq.request.headers.get('Authorization')).toBe(`Basic ${window.btoa('admin:correct-pass')}`);
    thirdReq.flush({ message: 'ok' });

    expect(result).toEqual({ message: 'ok' });
  });

  it('overrides stale browser Basic Auth header on non-GET requests when locked', () => {
    http.patch('/api/system', {}).subscribe({ error: () => {} });

    const req = httpMock.expectOne('/api/system');
    expect(req.request.headers.get('Authorization')).toBe('Basic locked');
    req.flush('Unauthorized', { status: 401, statusText: 'Unauthorized' });
    authService.submitPrompt(null);
  });
});

