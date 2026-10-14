import { HttpInterceptorFn } from '@angular/common/http';
import { inject } from '@angular/core';
import { AuthService } from './auth.service';
import { catchError, switchMap } from 'rxjs/operators';
import { throwError } from 'rxjs';

export const authInterceptor: HttpInterceptorFn = (req, next) => {
  const authService = inject(AuthService);
  const host = getRequestHost(req.url);
  const token = authService.getCredentials(host);

  let authReq = req;
  if (token) {
    authReq = req.clone({
      headers: req.headers.set('Authorization', `Basic ${token}`)
    });
  }

  return next(authReq).pipe(
    catchError((error) => {
      if (error.status === 401) {
        if (token) {
          authService.clearCredentials(host);
        }

        return authService.prompt(host, token ? 'Invalid username or password' : '').pipe(
          switchMap((newToken) => {
            if (newToken) {
              const retriedReq = req.clone({
                headers: req.headers.set('Authorization', `Basic ${newToken}`)
              });
              return next(retriedReq);
            }
            return throwError(() => error);
          })
        );
      }
      return throwError(() => error);
    })
  );
};

function getRequestHost(url: string): string {
  if (url.startsWith('http://') || url.startsWith('https://')) {
    try {
      const parsed = new URL(url);
      return parsed.host;
    } catch (e) {
      return window.location.host;
    }
  }
  return window.location.host;
}
