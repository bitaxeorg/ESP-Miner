import { Injectable } from '@angular/core';
import { HttpEvent, HttpHandler, HttpInterceptor, HttpRequest, HttpErrorResponse } from '@angular/common/http';
import { Observable, throwError } from 'rxjs';
import { catchError, switchMap } from 'rxjs/operators';
import { AuthService } from './auth.service';

@Injectable()
export class AuthInterceptor implements HttpInterceptor {
  constructor(private authService: AuthService) {}

  private getRequestHost(url: string): string {
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

  intercept(req: HttpRequest<any>, next: HttpHandler): Observable<HttpEvent<any>> {
    const host = this.getRequestHost(req.url);
    const token = this.authService.getCredentials(host);

    let authReq = req;
    if (token) {
      authReq = req.clone({
        headers: req.headers.set('Authorization', `Basic ${token}`)
      });
    }

    return next.handle(authReq).pipe(
      catchError((error: HttpErrorResponse) => {
        if (error.status === 401) {
          if (token) {
            this.authService.clearCredentials(host);
          }

          return this.authService.prompt(host, token ? 'Invalid username or password' : '').pipe(
            switchMap((newToken) => {
              if (newToken) {
                const retriedReq = req.clone({
                  headers: req.headers.set('Authorization', `Basic ${newToken}`)
                });
                return next.handle(retriedReq);
              }
              return throwError(() => error);
            })
          );
        }
        return throwError(() => error);
      })
    );
  }
}
