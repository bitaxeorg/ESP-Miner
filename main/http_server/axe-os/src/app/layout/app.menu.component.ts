import { Component, OnDestroy, OnInit } from '@angular/core';
import { Observable, Subject, shareReplay } from 'rxjs';
import { takeUntil } from 'rxjs/operators';
import { SystemApiService } from '../services/system.service';
import { LayoutService } from './service/app.layout.service';
import { SystemInfo as ISystemInfo } from 'src/app/generated';
import { I18nService } from '../i18n/i18n.service';

@Component({
  selector: 'app-menu',
  templateUrl: './app.menu.component.html'
})
export class AppMenuComponent implements OnInit, OnDestroy {
  public info$!: Observable<ISystemInfo>;

  model: any[] = [];
  private destroy$ = new Subject<void>();

  constructor(public layoutService: LayoutService,
    private systemService: SystemApiService,
    private i18n: I18nService
  ) {
    this.info$ = this.systemService.getInfo().pipe(shareReplay({ refCount: true, bufferSize: 1 }))
  }

  ngOnInit() {
    this.buildMenu();
    this.i18n.locale$.pipe(takeUntil(this.destroy$)).subscribe(() => {
      this.buildMenu();
    });
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }

  private buildMenu() {
    this.model = [
      {
        label: this.i18n.t('nav.menu'),
        items: [
          { label: this.i18n.t('nav.dashboard'), icon: 'pi pi-fw pi-home', routerLink: ['/'] },
          { label: this.i18n.t('nav.swarm'), icon: 'pi pi-fw pi-sitemap', routerLink: ['swarm'] },
          { label: this.i18n.t('nav.logs'), icon: 'pi pi-fw pi-list', routerLink: ['logs'] },
          { label: this.i18n.t('nav.system'), icon: 'pi pi-fw pi-wave-pulse', routerLink: ['system'] },
          { separator: true },

          { label: this.i18n.t('nav.pool'), icon: 'pi pi-fw pi-server', routerLink: ['pool'] },
          { label: this.i18n.t('nav.network'), icon: 'pi pi-fw pi-wifi', routerLink: ['network'] },
          { label: this.i18n.t('nav.theme'), icon: 'pi pi-fw pi-palette', routerLink: ['design'] },
          { label: this.i18n.t('nav.settings'), icon: 'pi pi-fw pi-cog', routerLink: ['settings'] },
          { label: this.i18n.t('nav.update'), icon: 'pi pi-fw pi-sync', routerLink: ['update'] },
          { separator: true },

          { label: this.i18n.t('nav.whitepaper'), icon: 'pi pi-fw pi-bitcoin', command: () => window.open('/bitcoin.pdf', '_blank') },
        ]
      }
    ];
  }
}
