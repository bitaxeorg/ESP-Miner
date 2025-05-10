import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable } from 'rxjs';

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
  constructor(private http: HttpClient) {}

  // Get theme settings from NVS storage
  getThemeSettings(): Observable<ThemeSettings> {
    return this.http.get<ThemeSettings>('/api/theme');
  }

  // Save theme settings to NVS storage
  saveThemeSettings(settings: ThemeSettings): Observable<void> {
    return this.http.post<void>('/api/theme', settings);
  }
}
