import { NgModule } from '@angular/core';
import { RouterModule, Routes } from '@angular/router';

import { HomeComponent } from './components/home/home.component';
import { LogsComponent } from './components/logs/logs.component';
import { SystemComponent } from './components/system/system.component';
import { UpdateComponent } from './components/update/update.component';
import { SettingsComponent } from './components/settings/settings.component';
import { NetworkComponent } from './components/network/network.component';
import { SwarmComponent } from './components/swarm/swarm.component';
import { DesignComponent } from './components/design/design.component';
import { PoolComponent } from './components/pool/pool.component';
import { AppLayoutComponent } from './layout/app.layout.component';
import { ApModeGuard } from './guards/ap-mode.guard';

const routes: Routes = [
  {
      path: 'ap',
      component: AppLayoutComponent,
      children: [
        {
          path: '',
          component: NetworkComponent,
          title: 'nav.network',
        }
      ]
  },
  {
    path: '',
    component: AppLayoutComponent,
    canActivate: [ApModeGuard],
    children: [
      {
        path: '',
        component: HomeComponent,
        title: 'common.app_name',
      },
      {
        path: 'logs',
        component: LogsComponent,
        title: 'nav.logs',
      },
      {
        path: 'system',
        component: SystemComponent,
        title: 'nav.system',
      },
      {
        path: 'update',
        component: UpdateComponent,
        title: 'nav.update',
      },
      {
        path: 'network',
        component: NetworkComponent,
        title: 'nav.network',
      },
      {
        path: 'settings',
        component: SettingsComponent,
        title: 'nav.settings',
      },
      {
        path: 'swarm',
        component: SwarmComponent,
        title: 'nav.swarm',
      },
      {
        path: 'design',
        component: DesignComponent,
        title: 'nav.theme',
      },
      {
        path: 'pool',
        component: PoolComponent,
        title: 'nav.pool',
      }
    ]
  },

];

@NgModule({
  imports: [RouterModule.forRoot(routes)],
  exports: [RouterModule]
})
export class AppRoutingModule { }
