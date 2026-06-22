import { Injectable } from '@angular/core';
import { DialogService } from 'primeng/dynamicdialog';
import { Observable, Subject } from 'rxjs';
import { LoginModalComponent } from '../components/login-modal/login-modal.component';

@Injectable({
  providedIn: 'root'
})
export class AuthService {
  private activePrompt: Subject<string | null> | null = null;

  constructor(private dialogService: DialogService) {}

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
    const currentPrompt = this.activePrompt;

    const ref = this.dialogService.open(LoginModalComponent, {
      header: `Authentication Required - ${host}`,
      width: '400px',
      closable: false,
      data: { error }
    });

    ref.onClose.subscribe((result: any) => {
      this.activePrompt = null;
      if (result) {
        const token = window.btoa(`${result.username}:${result.password}`);
        this.setCredentials(host, token);
        currentPrompt.next(token);
      } else {
        currentPrompt.next(null);
      }
      currentPrompt.complete();
    });

    return currentPrompt.asObservable();
  }
}
