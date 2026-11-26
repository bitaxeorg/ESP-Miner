import { HttpEvent, HttpHandlerFn, HttpInterceptorFn, HttpRequest } from '@angular/common/http';
import { inject } from '@angular/core';
import { Observable, throwError } from 'rxjs';
import { catchError, switchMap } from 'rxjs/operators';
import { AuthService } from './auth.service';

export const authInterceptor: HttpInterceptorFn = (req, next) => {
  if (!isAxeOsApiRequest(req.url)) {
    return next(req);
  }

  const authService = inject(AuthService);
  const host = authService.getHostFromUrl(req.url);
  const token = authService.getCredentials(host);

  return sendWithAuth(req, next, authService, host, token);
};

function sendWithAuth(
  req: HttpRequest<unknown>,
  next: HttpHandlerFn,
  authService: AuthService,
  host: string,
  attemptedToken: string | null
): Observable<HttpEvent<unknown>> {
  let headers = req.headers.set('X-Requested-With', 'XMLHttpRequest');
  if (attemptedToken) {
    headers = headers.set('Authorization', `Basic ${attemptedToken}`);
  } else if (req.method !== 'GET' && !authService.isReadAuthRequired(host)) {
    headers = headers.set('Authorization', 'Basic locked');
  }
  const authReq = req.clone({ headers });

  return next(authReq).pipe(
    catchError((error) => {
      if (error.status === 401) {
        const currentToken = authService.getCredentials(host);
        if (currentToken && currentToken !== attemptedToken) {
          return sendWithAuth(req, next, authService, host, currentToken);
        }

        if (attemptedToken && currentToken === attemptedToken) {
          authService.clearCredentials(host);
        }

        return authService.prompt(host, attemptedToken ? 'Invalid password' : '').pipe(
          switchMap((newToken) => {
            if (newToken) {
              return sendWithAuth(req, next, authService, host, newToken);
            }
            return throwError(() => error);
          })
        );
      }
      return throwError(() => error);
    })
  );
}

function isAxeOsApiRequest(url: string): boolean {
  if (url.startsWith('/api/')) {
    return true;
  }
  if (url.startsWith('http://') || url.startsWith('https://')) {
    try {
      return new URL(url).pathname.startsWith('/api/');
    } catch {
      return false;
    }
  }
  return false;
}
