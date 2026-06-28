import { Injectable, effect, signal } from '@angular/core';
import { BehaviorSubject, Subject } from 'rxjs';
import { ThemeService } from '../../services/theme.service';
import { LocalStorageService } from '../../local-storage.service';

const STATIC_MENU_DESKTOP_INACTIVE = 'STATIC_MENU_DESKTOP_INACTIVE'

export interface AppConfig {
    inputStyle: string;
    colorScheme: string;
    ripple: boolean;
    menuMode: string;
    scale: number;
}

interface LayoutState {
    staticMenuDesktopInactive: boolean;
    overlayMenuActive: boolean;
    profileSidebarVisible: boolean;
    configSidebarVisible: boolean;
    staticMenuMobileActive: boolean;
    menuHoverActive: boolean;
}

@Injectable({
    providedIn: 'root',
})
export class LayoutService {
    private darkTheme = {
        '--p-content-background': '#070D17',  // Very dark navy
        '--p-card-background': '#0B1219',     // Darker navy
        '--p-content-border-color': '#454d59', // Unified separator border
        '--card-border': '#1A2632',           // Darker card border
        '--p-text-color': 'rgba(255, 255, 255, 0.87)',
        '--p-text-muted-color': 'rgba(255, 255, 255, 0.6)',
        '--p-mask-background': 'rgba(0, 0, 0, 0.4)',
        '--p-overlay-select-background': '#0B1219',
        '--p-overlay-select-border-color': '#1A2632'
    };

    private lightTheme = {
        '--p-content-background': '#243447',  // Medium navy
        '--p-card-background': '#1a2632',     // Lighter navy
        '--p-content-border-color': '#454d59',
        '--card-border': '#2f4562',
        '--p-text-color': 'rgba(255, 255, 255, 0.9)',
        '--p-text-muted-color': 'rgba(255, 255, 255, 0.7)',
        '--p-mask-background': 'rgba(0, 0, 0, 0.2)',
        '--p-overlay-select-background': '#1a2632',
        '--p-overlay-select-border-color': '#2f4562'
    };

    private whiteTheme = {
        '--p-content-background': '#f8fafc',  // Softer blue-gray off-white (slate-100)
        '--p-card-background': '#f1f5f9',     // Card background is slate-50 (off-white, not pure white)
        '--p-content-border-color': '#e2e8f0', // Light border
        '--card-border': '#e2e8f0',           // Light card border
        '--p-text-color': '#0f172a',          // Dark text (slate-900)
        '--p-text-muted-color': '#64748b',    // Muted dark text (slate-500)
        '--p-mask-background': 'rgba(0, 0, 0, 0.2)',
        '--p-overlay-select-background': '#f1f5f9',
        '--p-overlay-select-border-color': '#e2e8f0'
    };

    _config: AppConfig = {
        ripple: false,
        inputStyle: 'outlined',
        menuMode: 'static',
        colorScheme: 'dark',
        scale: 14,
    };

    config = signal<AppConfig>(this._config);

    state: LayoutState = {
        staticMenuDesktopInactive: false,
        overlayMenuActive: false,
        profileSidebarVisible: false,
        configSidebarVisible: false,
        staticMenuMobileActive: false,
        menuHoverActive: false,
    };

    isWideView = signal<boolean>(this.localStorageService.getBool('DASHBOARD_WIDE_VIEW'));

    private overlayOpen = new Subject<any>();
    overlayOpen$ = this.overlayOpen.asObservable();

    private staticMenuDesktopInactive$ = new BehaviorSubject<boolean>(this.state.staticMenuDesktopInactive);

    constructor(
      private themeService: ThemeService,
      private localStorageService: LocalStorageService
    ) {
        // Load saved theme settings from NVS
        this.themeService.getThemeSettings().subscribe(
            settings => {
                if (settings) {
                    this._config = {
                        ...this._config,
                        colorScheme: settings.colorScheme,
                    };
                    // Apply accent colors dynamically
                    const accentColors = ThemeService.generateThemeVariables(settings.primaryColor);
                    Object.entries(accentColors).forEach(([key, value]) => {
                        document.documentElement.style.setProperty(key, value);
                    });
                } else {
                    // Save default red dark theme if no settings exist
                    const defaultPrimary = '#F80421';
                    this.themeService.saveThemeSettings({
                        colorScheme: 'dark',
                        primaryColor: defaultPrimary
                    }).subscribe();
                    
                    const accentColors = ThemeService.generateThemeVariables(defaultPrimary);
                    Object.entries(accentColors).forEach(([key, value]) => {
                        document.documentElement.style.setProperty(key, value);
                    });
                }
                // Update signal with config
                this.config.set(this._config);
                // Apply initial theme
                this.changeTheme();
            },
            error => {
                console.error('Error loading theme settings:', error);
                // Use default theme on error
                this.config.set(this._config);
                this.changeTheme();
            }
        );

        effect(() => {
            const config = this.config();
            this.changeTheme();
            this.changeScale(config.scale);
            this.handleStaticMenuDesktopInactivity();
        });
    }

    onMenuToggle() {
        if (this.isOverlay()) {
            this.state.overlayMenuActive = !this.state.overlayMenuActive;
            if (this.state.overlayMenuActive) {
                this.overlayOpen.next(null);
            }
        }

        if (this.isDesktop()) {
            this.state.staticMenuDesktopInactive =
                !this.state.staticMenuDesktopInactive;

            this.localStorageService.setBool(STATIC_MENU_DESKTOP_INACTIVE, this.state.staticMenuDesktopInactive);
            this.staticMenuDesktopInactive$.next(this.state.staticMenuDesktopInactive);
        } else {
            this.state.staticMenuMobileActive =
                !this.state.staticMenuMobileActive;

            if (this.state.staticMenuMobileActive) {
                this.overlayOpen.next(null);
            }
        }
    }

    showProfileSidebar() {
        this.state.profileSidebarVisible = !this.state.profileSidebarVisible;
        if (this.state.profileSidebarVisible) {
            this.overlayOpen.next(null);
        }
    }

    showConfigSidebar() {
        this.state.configSidebarVisible = true;
    }

    isOverlay() {
        return this.config().menuMode === 'overlay';
    }

    isDesktop() {
        return window.innerWidth > 991;
    }

    isMobile() {
        return !this.isDesktop();
    }

    changeTheme() {
        const config = this.config();

        // Apply light/dark/white theme variables
        let themeVars = this.darkTheme;
        if (config.colorScheme === 'light') {
            themeVars = this.lightTheme;
        } else if (config.colorScheme === 'white') {
            themeVars = this.whiteTheme;
        }
        Object.entries(themeVars).forEach(([key, value]) => {
            document.documentElement.style.setProperty(key, value);
        });

        // Toggle dark-mode class for PrimeNG Aura Theme
        if (config.colorScheme === 'white') {
            document.documentElement.classList.remove('dark-mode');
        } else {
            document.documentElement.classList.add('dark-mode');
        }

        // Load theme settings from NVS
        this.themeService.getThemeSettings().subscribe(
            settings => {
                if (settings && settings.primaryColor) {
                    const accentColors = ThemeService.generateThemeVariables(settings.primaryColor);
                    Object.entries(accentColors).forEach(([key, value]) => {
                        document.documentElement.style.setProperty(key, value);
                    });
                }
            },
            error => console.error('Error loading accent colors:', error)
        );
    }

    changeScale(value: number) {
        document.documentElement.style.fontSize = `${value}px`;
    }

    handleStaticMenuDesktopInactivity() {
        if (!this.isDesktop()) {
            return;
        }

        this.state.staticMenuDesktopInactive = this.localStorageService.getBool(STATIC_MENU_DESKTOP_INACTIVE);
        this.staticMenuDesktopInactive$.next(this.state.staticMenuDesktopInactive);
    }

    toggleWideView() {
        this.isWideView.set(!this.isWideView());
        this.localStorageService.setBool('DASHBOARD_WIDE_VIEW', this.isWideView());
    }

    getStaticMenuDesktopInactive$() {
      return this.staticMenuDesktopInactive$.asObservable();
    }
}
