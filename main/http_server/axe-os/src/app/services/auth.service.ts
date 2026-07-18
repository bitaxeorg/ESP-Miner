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
  private activePrompt: Subject<string | null> | null = null;
  private promptSubject = new BehaviorSubject<AuthPromptState | null>(null);
  public promptState$ = this.promptSubject.asObservable();

  constructor() {}

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
    localStorage.setItem('esp_miner_auth_tokens', JSON.stringify(tokens));
  }

  public clearCredentials(host: string): void {
    this.setCredentials(host, null);
  }

  private getTokens(): Record<string, string> {
    try {
      const stored = localStorage.getItem('esp_miner_auth_tokens');
      return stored ? JSON.parse(stored) : {};
    } catch (e) {
      return {};
    }
  }

  public prompt(host: string, error?: string): Observable<string | null> {
    if (this.activePrompt) {
      return this.activePrompt.asObservable();
    }

    this.activePrompt = new Subject<string | null>();
    this.promptSubject.next({ host, error });

    return this.activePrompt.asObservable();
  }

  public submitPrompt(result: any | null): void {
    if (!this.activePrompt) {
      return;
    }
    const currentPrompt = this.activePrompt;
    const currentState = this.promptSubject.value;

    this.activePrompt = null;
    this.promptSubject.next(null);

    if (result && currentState) {
      const token = window.btoa(`${result.username}:${result.password}`);
      this.setCredentials(currentState.host, token);
      currentPrompt.next(token);
    } else {
      currentPrompt.next(null);
    }
    currentPrompt.complete();
  }
}
