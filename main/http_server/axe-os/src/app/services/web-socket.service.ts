import { Injectable } from '@angular/core';
import { defer, Observable, throwError } from 'rxjs';
import { filter, retry, take } from 'rxjs/operators';
import { webSocket } from 'rxjs/webSocket';
import { AuthService } from './auth.service';

@Injectable({
  providedIn: 'root'
})
export class WebsocketService {
  public readonly ws$: Observable<string>;

  constructor(private authService: AuthService) {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = window.location.host;

    this.ws$ = defer(() => {
      const token = this.authService.getCredentials(host);
      // TODO(security): Browser WebSocket API cannot set custom Authorization headers; token is sent via query param on LAN WS.
      const authParam = token ? `?auth=${encodeURIComponent(token)}` : '';
      return webSocket<string>({
        url: `${protocol}//${host}/api/ws${authParam}`,
        deserializer: (e: MessageEvent) => e.data
      });
    }).pipe(
      retry({
        delay: (err) => {
          if (this.authService.isPromptActive(host)) {
            return this.authService.credentialsChanged$.pipe(
              filter(changedHost => changedHost === host),
              take(1)
            );
          }
          return throwError(() => err);
        }
      })
    );
  }
}
