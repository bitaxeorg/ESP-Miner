import { Component, OnInit, OnDestroy } from '@angular/core';
import { Subject } from 'rxjs';
import { takeUntil } from 'rxjs/operators';
import { LayoutService } from '../../layout/service/app.layout.service';
import { ThemeService } from '../../services/theme.service';

interface ThemeOption {
  name: string;
  primaryColor: string;
}

@Component({
  selector: 'app-theme-config',
  templateUrl: './theme-config.component.html',
  styleUrls: ['./design-component.scss']
})
export class ThemeConfigComponent implements OnInit {
  selectedScheme: string;
  currentColor: string = '';
  themes: ThemeOption[] = [
    // === Reds & Oranges (energetic & warm) ===
    { name: 'Bright Red', primaryColor: '#F80421' },
    { name: 'Coral Red', primaryColor: '#FF5252' },
    { name: 'Tomato Red', primaryColor: '#FF6347' },
    { name: 'Burnt Orange', primaryColor: '#E64A19' },
    { name: 'Vivid Orange', primaryColor: '#FF7A00' },
    { name: 'Bitcoin Orange', primaryColor: '#F7931A' },

    // === Yellows & Golds ===
    { name: 'Golden Amber', primaryColor: '#FFB300' },
    { name: 'Metallic Gold', primaryColor: '#E6B800' },
    { name: 'Lemon Yellow', primaryColor: '#FFDD00' },
    { name: 'Chartreuse', primaryColor: '#C0D000' },

    // === Greens ===
    { name: 'Emerald Green', primaryColor: '#4CAF50' },
    { name: 'Lime Green', primaryColor: '#8BC34A' },
    { name: 'Mint Green', primaryColor: '#4DB6AC' },
    { name: 'Spring Green', primaryColor: '#66FF99' },
    { name: 'Olive', primaryColor: '#808000' },

    // === Teals & Cyans ===
    { name: 'Forest Teal', primaryColor: '#00796B' },
    { name: 'Bright Cyan', primaryColor: '#00BCD4' },
    { name: 'Turquoise', primaryColor: '#40E0D0' },
    { name: 'Aqua', primaryColor: '#00FFFF' },

    // === Blues ===
    { name: 'Sky Blue', primaryColor: '#29B6F6' },
    { name: 'Light Blue', primaryColor: '#81D4FA' },
    { name: 'Deep Blue', primaryColor: '#1976D2' },
    { name: 'Royal Blue', primaryColor: '#2A5CDB' },
    { name: 'Indigo', primaryColor: '#3F51B5' },
    { name: 'Navy Blue', primaryColor: '#1A3C7E' },
    { name: 'Steel Blue', primaryColor: '#4682B4' },

    // === Purples & Violets ===
    { name: 'Hot Pink', primaryColor: '#E91E63' },
    { name: 'Rose Pink', primaryColor: '#FF4081' },
    { name: 'Magenta Purple', primaryColor: '#9C27B0' },
    { name: 'Deep Violet', primaryColor: '#512DA8' },
    { name: 'Lavender', primaryColor: '#9575CD' },
    { name: 'Plum', primaryColor: '#9B2A6E' },
    { name: 'Orchid', primaryColor: '#DA70D6' },

    // === Browns & Neutrals ===
    { name: 'Earth Brown', primaryColor: '#795548' },
    { name: 'Warm Taupe', primaryColor: '#8D6E63' },
    { name: 'Chocolate', primaryColor: '#5D4037' },
    { name: 'Slate Grey', primaryColor: '#607D8B' },
    { name: 'Blue Grey', primaryColor: '#455A64' },
    { name: 'Cool Grey', primaryColor: '#78909C' },

    // === Additional Distinct Accents ===
    { name: 'Peach', primaryColor: '#FFAB91' },
    { name: 'Apricot', primaryColor: '#FFD8B1' },
    { name: 'Sand', primaryColor: '#E0C9A8' },
    { name: 'Terracotta', primaryColor: '#E07A5F' },
    { name: 'Rust', primaryColor: '#B74C2E' },
    { name: 'Crimson', primaryColor: '#B71C1C' },
    { name: 'Berry', primaryColor: '#8E24AA' },
    { name: 'Periwinkle', primaryColor: '#8C9EFF' },
    { name: 'Electric Purple', primaryColor: '#BB86FC' },
    { name: 'Neon Green', primaryColor: '#39FF14' },
    { name: 'Acid Lime', primaryColor: '#CCFF00' }
  ];

  private destroy$ = new Subject<void>();

  constructor(
    public layoutService: LayoutService,
    private themeService: ThemeService
  ) {
    this.selectedScheme = this.layoutService.config().colorScheme;
  }

  ngOnInit() {
    this.themeService.getThemeSettings()
      .pipe(takeUntil(this.destroy$))
      .subscribe({
        next: (settings) => {
          if (settings) {
            if (settings.colorScheme) {
              this.selectedScheme = settings.colorScheme;
            }
            if (settings.primaryColor) {
              this.currentColor = settings.primaryColor.toUpperCase();
              const accentColors = ThemeService.generateThemeVariables(this.currentColor);
              this.applyThemeColors(accentColors);
            }
          }
        },
        error: (error) => console.error('Error loading theme settings:', error)
      });
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
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

    this.themeService.saveThemeSettings({ colorScheme: scheme, primaryColor: this.currentColor })
      .pipe(takeUntil(this.destroy$))
      .subscribe({
        error: (error) => console.error('Error saving theme settings:', error)
      });
  }

  changeTheme(theme: ThemeOption) {
    this.currentColor = theme.primaryColor;
    const accentColors = ThemeService.generateThemeVariables(this.currentColor);
    this.applyThemeColors(accentColors);

    this.themeService.saveThemeSettings({
      colorScheme: this.selectedScheme,
      primaryColor: this.currentColor
    }).pipe(takeUntil(this.destroy$))
      .subscribe({
        error: (error) => console.error('Error saving theme settings:', error)
      });
  }

  onCustomColorChange(event: any) {
    const color = event.target.value;
    this.currentColor = color.toUpperCase();
    const accentColors = ThemeService.generateThemeVariables(this.currentColor);
    this.applyThemeColors(accentColors);

    this.themeService.saveThemeSettings({
      colorScheme: this.selectedScheme,
      primaryColor: this.currentColor
    }).pipe(takeUntil(this.destroy$))
      .subscribe({
        error: (error) => console.error('Error saving theme settings:', error)
      });
  }
}
