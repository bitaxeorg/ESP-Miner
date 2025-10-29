import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { environment } from '../../environments/environment';
import { BehaviorSubject, Observable, of } from 'rxjs';
import { catchError, tap } from 'rxjs/operators';

export interface ThemeSettings {
  colorScheme: string;
  accentColors?: {
    [key: string]: string;
  };
}

@Injectable({
  providedIn: 'root'
})
export class ThemeService {
  private readonly mockSettings: ThemeSettings = {
    colorScheme: 'dark',
    accentColors: {
      '--primary-color': '#F80421',
      '--primary-color-text': '#ffffff',
      '--highlight-bg': '#F80421',
      '--highlight-text-color': '#ffffff',
      '--focus-ring': '0 0 0 0.2rem rgba(255,64,50,0.2)',
      // PrimeNG Slider
      '--slider-bg': '#dee2e6',
      '--slider-range-bg': '#F80421',
      '--slider-handle-bg': '#F80421',
      // Progress Bar
      '--progressbar-bg': '#dee2e6',
      '--progressbar-value-bg': '#F80421',
      // PrimeNG Checkbox
      '--checkbox-border': '#F80421',
      '--checkbox-bg': '#F80421',
      '--checkbox-hover-bg': '#e63c2e',
      // PrimeNG Button
      '--button-bg': '#F80421',
      '--button-hover-bg': '#e63c2e',
      '--button-focus-shadow': '0 0 0 2px #ffffff, 0 0 0 4px #F80421',
      // Toggle button
      '--togglebutton-bg': '#F80421',
      '--togglebutton-border': '1px solid #F80421',
      '--togglebutton-hover-bg': '#e63c2e',
      '--togglebutton-hover-border': '1px solid #e63c2e',
      '--togglebutton-text-color': '#ffffff'
    }
  };

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
