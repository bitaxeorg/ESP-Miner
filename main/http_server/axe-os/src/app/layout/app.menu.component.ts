import { Component, OnInit } from '@angular/core';
import { Observable } from 'rxjs';
import { LiveDataService } from '../services/live-data.service';
import { LayoutService } from './service/app.layout.service';
import { SystemInfo as ISystemInfo } from 'src/app/generated/models';

@Component({
    selector: 'app-menu',
    templateUrl: './app.menu.component.html',
    standalone: false
})
export class AppMenuComponent implements OnInit {
  public info$: Observable<ISystemInfo>;

  model: any[] = [];

  constructor(
    public layoutService: LayoutService,
    private liveDataService: LiveDataService
  ) {
    this.info$ = this.liveDataService.info$;
  }

  ngOnInit() {
    this.model = [
      {
        label: 'Menu',
        items: [
          { label: 'Dashboard', icon: 'pi pi-fw pi-home', routerLink: ['/'] },
          { label: 'Scoreboard', icon: 'pi pi-fw pi-trophy', routerLink: ['scoreboard'] },
          { label: 'Swarm', icon: 'pi pi-fw pi-list', routerLink: ['swarm'] },
          { label: 'Logs', icon: 'pi pi-fw pi-clipboard', routerLink: ['logs'] },
          { label: 'System', icon: 'pi pi-fw pi-wave-pulse', routerLink: ['system'] },
          { separator: true },

          { label: 'Pool', icon: 'pi pi-fw pi-server', routerLink: ['pool'] },
          { label: 'Network', icon: 'pi pi-fw pi-globe', routerLink: ['network'] },
          { label: 'Theme', icon: 'pi pi-fw pi-palette', routerLink: ['design'] },
          { label: 'Settings', icon: 'pi pi-fw pi-cog', routerLink: ['settings'] },
          { label: 'Update', icon: 'pi pi-fw pi-sync', routerLink: ['update'] },
          { separator: true },

          { label: 'Whitepaper', icon: 'pi pi-fw pi-bitcoin', command: () => window.open('/bitcoin.pdf', '_blank') },
        ]
      }
    ];
  }
}
