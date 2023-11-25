import { NgModule } from '@angular/core';
import { RouterModule, Routes } from '@angular/router';

import { EditComponent } from './components/edit/edit.component';
import { HomeComponent } from './components/home/home.component';
import { SwarmComponent } from './components/swarm/swarm.component';

const routes: Routes = [
  {
    path: '',
    component: HomeComponent
  },
  {
    path: 'edit',
    component: EditComponent
  },
  {
    path: 'swarm',
    component: SwarmComponent
  }
];

@NgModule({
  imports: [RouterModule.forRoot(routes)],
  exports: [RouterModule]
})
export class AppRoutingModule { }
