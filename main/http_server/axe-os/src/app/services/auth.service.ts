import { Injectable } from '@angular/core';
import { BehaviorSubject, Observable, Subject } from 'rxjs';

export interface AuthPromptState {
  host: string;
  error?: string;
}

@Injectable({
  providedIn: 'root'
})
export class AuthService {
  private pendingPrompts = new Map<string, Subject<string | null>>();
  private promptQueue: AuthPromptState[] = [];
  private readAuthHosts = new Set<string>();
  private promptSubject = new BehaviorSubject<AuthPromptState | null>(null);
  private credentialsChangedSubject = new Subject<string>();

  public readonly promptState$ = this.promptSubject.asObservable();
  public readonly credentialsChanged$ = this.credentialsChangedSubject.asObservable();

  public getHostFromUrl(url: string): string {
    if (url && (url.startsWith('http://') || url.startsWith('https://'))) {
      try {
        return new URL(url).host;
      } catch {
        return window.location.host;
      }
    }
    return window.location.host;
  }

  public encodeBasicToken(password: string): string {
    const bytes = new TextEncoder().encode(`admin:${password}`);
    let binary = '';
    for (const byte of bytes) {
      binary += String.fromCharCode(byte);
    }
    return window.btoa(binary);
  }

  public getCredentials(host: string): string | null {
    const tokens = this.getTokens();
    return tokens[host] || null;
  }

  public setCredentials(host: string, token: string | null): void {
    const tokens = this.getTokens();
    if (token) {
      tokens[host] = token;
    } else {
      delete tokens[host];
    }
    // TODO(security): Stored per-host in localStorage to support cross-origin LAN Swarm management without HTTPS cookies.
    localStorage.setItem('esp_miner_auth_tokens', JSON.stringify(tokens));
    this.credentialsChangedSubject.next(host);
  }

  public clearCredentials(host: string): void {
    this.setCredentials(host, null);
  }

  public setReadAuthRequired(host: string, required: boolean): void {
    if (required) {
      this.readAuthHosts.add(host);
    } else {
      this.readAuthHosts.delete(host);
    }
  }

  public isReadAuthRequired(host: string): boolean {
    return this.readAuthHosts.has(host);
  }

  public isPromptActive(host: string): boolean {
    return this.pendingPrompts.has(host);
  }

  private getTokens(): Record<string, string> {
    try {
      const stored = localStorage.getItem('esp_miner_auth_tokens');
      return stored ? JSON.parse(stored) : {};
    } catch {
      return {};
    }
  }

  public prompt(host: string, error?: string): Observable<string | null> {
    const existing = this.pendingPrompts.get(host);
    if (existing) {
      if (error) {
        if (this.promptSubject.value?.host === host && !this.promptSubject.value.error) {
          this.promptSubject.next({ host, error });
        } else {
          const queued = this.promptQueue.find((q) => q.host === host);
          if (queued && !queued.error) {
            queued.error = error;
          }
        }
      }
      return existing.asObservable();
    }

    const subject = new Subject<string | null>();
    this.pendingPrompts.set(host, subject);

    if (!this.promptSubject.value) {
      this.promptSubject.next({ host, error });
    } else {
      this.promptQueue.push({ host, error });
    }

    return subject.asObservable();
  }

  public submitPrompt(password: string | null): void {
    const currentState = this.promptSubject.value;
    if (!currentState) {
      return;
    }

    const subject = this.pendingPrompts.get(currentState.host);
    this.pendingPrompts.delete(currentState.host);

    const nextState = this.promptQueue.shift() ?? null;
    this.promptSubject.next(nextState);

    if (!subject) {
      return;
    }

    if (password) {
      const token = this.encodeBasicToken(password);
      this.setCredentials(currentState.host, token);
      subject.next(token);
    } else {
      subject.next(null);
    }
    subject.complete();
  }
}
