import { Injectable, effect, signal } from '@angular/core';
import { BehaviorSubject, Subject } from 'rxjs';
import { ThemeService } from '../../services/theme.service';
import { LocalStorageService } from '../../local-storage.service';

const STATIC_MENU_DESKTOP_INACTIVE = 'STATIC_MENU_DESKTOP_INACTIVE'

export interface AppConfig {
    colorScheme: string;
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
        '--color-bg-content': '#070D17',  // Very dark navy
        '--color-bg-card': '#0B1219',     // Darker navy
        '--color-border-content': '#454d59', // Unified separator border
        '--card-border': '#1A2632',           // Darker card border
        '--surface-border': '#192730',        // Verified border color from master
        '--color-text-main': 'rgba(255, 255, 255, 0.87)',
        '--color-text-secondary': 'rgba(255, 255, 255, 0.6)',
        '--color-mask-bg': 'rgba(0, 0, 0, 0.4)',
        '--color-overlay-select-bg': '#0B1219',
        '--color-overlay-select-border': '#1A2632'
    };

    private lightTheme = {
        '--color-bg-content': '#243447',  // Medium navy
        '--color-bg-card': '#1a2632',     // Lighter navy
        '--color-border-content': '#454d59',
        '--card-border': '#2f4562',
        '--surface-border': '#2f4562',        // Verified border color from master
        '--color-text-main': 'rgba(255, 255, 255, 0.9)',
        '--color-text-secondary': 'rgba(255, 255, 255, 0.7)',
        '--color-mask-bg': 'rgba(0, 0, 0, 0.2)',
        '--color-overlay-select-bg': '#1a2632',
        '--color-overlay-select-border': '#2f4562'
    };

    private whiteTheme = {
        '--color-bg-content': '#f8fafc',  // Softer blue-gray off-white (slate-100)
        '--color-bg-card': '#f1f5f9',     // Card background is slate-50 (off-white, not pure white)
        '--color-border-content': '#e2e8f0', // Light border
        '--card-border': '#e2e8f0',           // Light card border
        '--surface-border': '#cbd5e1',        // Slate-300 dark grey border for white theme
        '--color-text-main': '#0f172a',          // Dark text (slate-900)
        '--color-text-secondary': '#64748b',    // Muted dark text (slate-500)
        '--color-mask-bg': 'rgba(0, 0, 0, 0.2)',
        '--color-overlay-select-bg': '#f1f5f9',
        '--color-overlay-select-border': '#e2e8f0'
    };

    _config: AppConfig = {
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

        // Toggle dark-mode class for theme switching
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
