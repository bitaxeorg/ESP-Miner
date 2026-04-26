import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { environment } from '../../environments/environment';
import { BehaviorSubject, Observable, of } from 'rxjs';
import { catchError, tap } from 'rxjs/operators';

export interface ThemeSettings {
  colorScheme: string;
  primaryColor: string;
}

@Injectable({
  providedIn: 'root'
})
export class ThemeService {
  private readonly mockSettings: ThemeSettings = {
    colorScheme: 'dark',
    primaryColor: '#F80421'
  };

  static generateThemeVariables(primaryColor: string): { [key: string]: string } {
    const hoverColor = this.shadeColor(primaryColor, -10);
    const focusRingColor = this.hexToRgba(primaryColor, 0.2);
    
    return {
      '--primary-color': primaryColor,
      '--primary-color-text': '#ffffff',
      '--highlight-bg': primaryColor,
      '--highlight-text-color': '#ffffff',
      '--focus-ring': `0 0 0 0.2rem ${focusRingColor}`,
      '--slider-bg': '#dee2e6',
      '--slider-range-bg': primaryColor,
      '--slider-handle-bg': primaryColor,
      '--progressbar-bg': '#dee2e6',
      '--progressbar-value-bg': primaryColor,
      '--checkbox-border': primaryColor,
      '--checkbox-bg': primaryColor,
      '--checkbox-hover-bg': hoverColor,
      '--button-bg': primaryColor,
      '--button-hover-bg': hoverColor,
      '--button-focus-shadow': `0 0 0 2px #ffffff, 0 0 0 4px ${primaryColor}`,
      '--togglebutton-bg': primaryColor,
      '--togglebutton-border': `1px solid ${primaryColor}`,
      '--togglebutton-hover-bg': hoverColor,
      '--togglebutton-hover-border': `1px solid ${hoverColor}`,
      '--togglebutton-text-color': '#ffffff'
    };
  }

  private static shadeColor(color: string, percent: number): string {
    let R = parseInt(color.substring(1, 3), 16);
    let G = parseInt(color.substring(3, 5), 16);
    let B = parseInt(color.substring(5, 7), 16);

    R = Math.floor(R * (100 + percent) / 100);
    G = Math.floor(G * (100 + percent) / 100);
    B = Math.floor(B * (100 + percent) / 100);

    R = Math.min(255, Math.max(0, R));
    G = Math.min(255, Math.max(0, G));
    B = Math.min(255, Math.max(0, B));

    const RR = R.toString(16).padStart(2, '0');
    const GG = G.toString(16).padStart(2, '0');
    const BB = B.toString(16).padStart(2, '0');

    return "#" + RR + GG + BB;
  }

  private static hexToRgba(hex: string, alpha: number): string {
    const r = parseInt(hex.substring(1, 3), 16);
    const g = parseInt(hex.substring(3, 5), 16);
    const b = parseInt(hex.substring(5, 7), 16);
    return `rgba(${r}, ${g}, ${b}, ${alpha})`;
  }

  private themeSettingsSubject = new BehaviorSubject<ThemeSettings>(this.mockSettings);
  private themeSettings$ = this.themeSettingsSubject.asObservable();

  constructor(private http: HttpClient) {
    if (environment.production) {
      this.http.get<ThemeSettings>('/api/theme').pipe(
        catchError(() => of(this.mockSettings)),
        tap(settings => this.themeSettingsSubject.next(settings))
      ).subscribe();
    }
  }

  getThemeSettings(): Observable<ThemeSettings> {
    return this.themeSettings$;
  }

  saveThemeSettings(settings: ThemeSettings): Observable<void> {
    if (environment.production) {
      return this.http.post<void>('/api/theme', settings).pipe(
        tap(() => this.themeSettingsSubject.next(settings))
      );
    } else {
      this.themeSettingsSubject.next(settings);
      return of(void 0);
    }
  }
}
