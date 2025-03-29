import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable } from 'rxjs';
import { environment } from '../../environments/environment';
import { of } from 'rxjs';
import { catchError, shareReplay } from 'rxjs/operators';

export interface ThemeSettings {
  colorScheme: string;
  theme: string;
  accentColors?: {
    [key: string]: string;
  };
}

@Injectable({
  providedIn: 'root'
})
export class ThemeService {
  private themeSettings$ = this.http.get<ThemeSettings>('/api/theme').pipe(
    catchError(() => of(this.mockSettings)),
    shareReplay({ 
      bufferSize: 1, 
      refCount: true, 
      windowTime: 1000 // 1 second cache
    })
  );

  private readonly mockSettings: ThemeSettings = {
    colorScheme: 'dark',
    theme: 'dark',
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

  constructor(private http: HttpClient) {}

  // Get theme settings from NVS storage
  getThemeSettings(): Observable<ThemeSettings> {
    if (!environment.production) {
      return of(this.mockSettings);
    }
    return this.themeSettings$;
  }

  // Save theme settings to NVS storage
  saveThemeSettings(settings: ThemeSettings): Observable<void> {
    if (environment.production) {
      return this.http.post<void>('/api/theme', settings);
    } else {
      return of(void 0);
    }
  }
}
