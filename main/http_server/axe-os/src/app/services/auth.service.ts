import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { Observable, of } from 'rxjs';
import { delay } from 'rxjs/operators';
import { environment } from '../../environments/environment';

export interface AuthInfo {
  // 0 = authentication disabled, 1 = enabled. Matches the backend's numeric flag.
  enabled: number;
  username: string;
}

export interface AuthUpdateResponse {
  enabled: number;
  message: string;
}

@Injectable({
  providedIn: 'root'
})
export class AuthService {

  constructor(private httpClient: HttpClient) { }

  /** Report whether web authentication is currently enabled. */
  public getAuthInfo(uri: string = ''): Observable<AuthInfo> {
    if (environment.mock) {
      return of<AuthInfo>({ enabled: 0, username: 'admin' }).pipe(delay(200));
    }
    return this.httpClient.get<AuthInfo>(`${uri}/api/system/auth`);
  }

  /**
   * Set, change or clear the web authentication password.
   * An empty newPassword disables authentication.
   */
  public setCredentials(username: string, newPassword: string, uri: string = ''): Observable<AuthUpdateResponse> {
    if (environment.mock) {
      const enabled = newPassword != null && newPassword.length > 0 ? 1 : 0;
      return of<AuthUpdateResponse>({
        enabled,
        message: enabled ? 'Authentication enabled (mock)' : 'Authentication disabled (mock)'
      }).pipe(delay(200));
    }
    return this.httpClient.post<AuthUpdateResponse>(`${uri}/api/system/auth`, { username, newPassword });
  }
}
