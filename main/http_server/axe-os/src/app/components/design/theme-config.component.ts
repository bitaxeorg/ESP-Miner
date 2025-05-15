import { Component, OnInit } from '@angular/core';
import { LayoutService } from '../../layout/service/app.layout.service';
import { ThemeService } from '../../services/theme.service';

interface ThemeOption {
  name: string;
  primaryColor: string;
  accentColors: {
    [key: string]: string;
  };
}

@Component({
  selector: 'app-theme-config',
  template: `
    <div class="card">
      <h5>Theme Configuration</h5>
      <div class="grid">
        <div class="col-12">
          <h6>Color Scheme</h6>
          <div class="flex gap-3">
            <div class="flex align-items-center">
              <p-radioButton name="colorScheme" [value]="'dark'" [(ngModel)]="selectedScheme"
                (onClick)="changeColorScheme('dark')" inputId="dark"></p-radioButton>
              <label for="dark" class="ml-2">Dark</label>
            </div>
            <div class="flex align-items-center">
              <p-radioButton name="colorScheme" [value]="'light'" [(ngModel)]="selectedScheme"
                (onClick)="changeColorScheme('light')" inputId="light"></p-radioButton>
              <label for="light" class="ml-2">Light</label>
            </div>
          </div>
        </div>

        <div class="col-12 mt-4">
          <h6>Theme Colors</h6>
          <div class="grid gap-2">
            <div *ngFor="let theme of themes" class="col-2 theme-color">
              <button pButton [class]="'p-button-rounded p-button-text color-dot'"
                      [style.backgroundColor]="theme.primaryColor"
                      style="width: 2rem; height: 2rem; border: none;"
                      (click)="changeTheme(theme)">
                <i *ngIf="theme.primaryColor === currentColor" class="pi pi-check selected-icon"></i>
              </button>
              <div class="text-sm mt-1">{{theme.name}}</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  `,
  styleUrls: ['./design-component.scss'],
  standalone: false
})
export class ThemeConfigComponent implements OnInit {
  selectedScheme: string;
  currentColor: string = '';
  themes: ThemeOption[] = [
    {
      name: 'Orange',
      primaryColor: '#F7931A',
      accentColors: {
        '--primary-color': '#F7931A',
        '--primary-color-text': '#ffffff',
        '--highlight-bg': '#F7931A',
        '--highlight-text-color': '#ffffff',
        '--focus-ring': '0 0 0 0.2rem rgba(247,147,26,0.2)',
        // PrimeNG Slider
        '--slider-bg': '#dee2e6',
        '--slider-range-bg': '#F7931A',
        '--slider-handle-bg': '#F7931A',
        // Progress Bar
        '--progressbar-bg': '#dee2e6',
        '--progressbar-value-bg': '#F7931A',
        // PrimeNG Checkbox
        '--checkbox-border': '#F7931A',
        '--checkbox-bg': '#F7931A',
        '--checkbox-hover-bg': '#e58617',
        // PrimeNG Button
        '--button-bg': '#F7931A',
        '--button-hover-bg': '#e58617',
        '--button-focus-shadow': '0 0 0 2px #ffffff, 0 0 0 4px #F7931A',
        // Toggle button
        '--togglebutton-bg': '#F7931A',
        '--togglebutton-border': '1px solid #F7931A',
        '--togglebutton-hover-bg': '#e58617',
        '--togglebutton-hover-border': '1px solid #e58617',
        '--togglebutton-text-color': '#ffffff'
      }
    },
    {
      name: 'Red',
      primaryColor: '#F80421',
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
    },
    {
      name: 'Blue',
      primaryColor: '#2196f3',
      accentColors: {
        '--primary-color': '#2196f3',
        '--primary-color-text': '#ffffff',
        '--highlight-bg': '#2196f3',
        '--highlight-text-color': '#ffffff',
        '--focus-ring': '0 0 0 0.2rem rgba(33,150,243,0.2)',
        // PrimeNG Slider
        '--slider-bg': '#dee2e6',
        '--slider-range-bg': '#2196f3',
        '--slider-handle-bg': '#2196f3',
        // Progress Bar
        '--progressbar-bg': '#dee2e6',
        '--progressbar-value-bg': '#2196f3',
        // PrimeNG Checkbox
        '--checkbox-border': '#2196f3',
        '--checkbox-bg': '#2196f3',
        '--checkbox-hover-bg': '#1e88e5',
        // PrimeNG Button
        '--button-bg': '#2196f3',
        '--button-hover-bg': '#1e88e5',
        '--button-focus-shadow': '0 0 0 2px #ffffff, 0 0 0 4px #2196f3',
        // Toggle button
        '--togglebutton-bg': '#2196f3',
        '--togglebutton-border': '1px solid #2196f3',
        '--togglebutton-hover-bg': '#1e88e5',
        '--togglebutton-hover-border': '1px solid #1e88e5',
        '--togglebutton-text-color': '#ffffff'
      }
    },
    {
      name: 'Green',
      primaryColor: '#4caf50',
      accentColors: {
        '--primary-color': '#4caf50',
        '--primary-color-text': '#ffffff',
        '--highlight-bg': '#4caf50',
        '--highlight-text-color': '#ffffff',
        '--focus-ring': '0 0 0 0.2rem rgba(76,175,80,0.2)',
        // PrimeNG Slider
        '--slider-bg': '#dee2e6',
        '--slider-range-bg': '#4caf50',
        '--slider-handle-bg': '#4caf50',
        // Progress Bar
        '--progressbar-bg': '#dee2e6',
        '--progressbar-value-bg': '#4caf50',
        // PrimeNG Checkbox
        '--checkbox-border': '#4caf50',
        '--checkbox-bg': '#4caf50',
        '--checkbox-hover-bg': '#43a047',
        // PrimeNG Button
        '--button-bg': '#4caf50',
        '--button-hover-bg': '#43a047',
        '--button-focus-shadow': '0 0 0 2px #ffffff, 0 0 0 4px #4caf50',
        // Toggle button
        '--togglebutton-bg': '#4caf50',
        '--togglebutton-border': '1px solid #4caf50',
        '--togglebutton-hover-bg': '#43a047',
        '--togglebutton-hover-border': '1px solid #43a047',
        '--togglebutton-text-color': '#ffffff'
      }
    },
    {
      name: 'Purple',
      primaryColor: '#9c27b0',
      accentColors: {
        '--primary-color': '#9c27b0',
        '--primary-color-text': '#ffffff',
        '--highlight-bg': '#9c27b0',
        '--highlight-text-color': '#ffffff',
        '--focus-ring': '0 0 0 0.2rem rgba(156,39,176,0.2)',
        // PrimeNG Slider
        '--slider-bg': '#dee2e6',
        '--slider-range-bg': '#9c27b0',
        '--slider-handle-bg': '#9c27b0',
        // Progress Bar
        '--progressbar-bg': '#dee2e6',
        '--progressbar-value-bg': '#9c27b0',
        // PrimeNG Checkbox
        '--checkbox-border': '#9c27b0',
        '--checkbox-bg': '#9c27b0',
        '--checkbox-hover-bg': '#8e24aa',
        // PrimeNG Button
        '--button-bg': '#9c27b0',
        '--button-hover-bg': '#8e24aa',
        '--button-focus-shadow': '0 0 0 2px #ffffff, 0 0 0 4px #9c27b0',
        // Toggle button
        '--togglebutton-bg': '#9c27b0',
        '--togglebutton-border': '1px solid #9c27b0',
        '--togglebutton-hover-bg': '#8e24aa',
        '--togglebutton-hover-border': '1px solid #8e24aa',
        '--togglebutton-text-color': '#ffffff'
      }
    }
  ];

  constructor(
    public layoutService: LayoutService,
    private themeService: ThemeService
  ) {
    this.selectedScheme = this.layoutService.config().colorScheme;
  }

  ngOnInit() {
    // Load saved theme settings from NVS
    this.themeService.getThemeSettings().subscribe(
      settings => {
        if (settings) {
          // Apply saved color scheme
          if (settings.colorScheme) {
            this.selectedScheme = settings.colorScheme;
          }
          // Apply accent colors if they exist
          if (settings.accentColors) {
            this.applyThemeColors(settings.accentColors);
          }
        }
      },
      error => console.error('Error loading theme settings:', error)
    );
  }

  private applyThemeColors(colors: { [key: string]: string }) {
    Object.entries(colors).forEach(([key, value]) => {
      document.documentElement.style.setProperty(key, value);
    });
  }

  changeColorScheme(scheme: string) {
    this.selectedScheme = scheme;
    const config = { ...this.layoutService.config() };
    config.colorScheme = scheme;
    this.layoutService.config.set(config);
    // Save color scheme to NVS
    this.themeService.saveThemeSettings({
      colorScheme: scheme
    }).subscribe(
      () => { },
      error => console.error('Error saving theme settings:', error)
    );
  }

  changeTheme(theme: ThemeOption) {
    // Update CSS variables
    this.applyThemeColors(theme.accentColors);
    this.currentColor = theme.primaryColor;
    // Save theme settings to NVS
    this.themeService.saveThemeSettings({
      colorScheme: this.selectedScheme,
      accentColors: theme.accentColors
    }).subscribe(
      () => { },
      error => console.error('Error saving theme settings:', error)
    );
  }
}
