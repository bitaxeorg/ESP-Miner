import 'chartjs-adapter-moment';

import { CommonModule, HashLocationStrategy, LocationStrategy } from '@angular/common';
import { HttpClientModule } from '@angular/common/http';
import { NgModule } from '@angular/core';
import { FormsModule, ReactiveFormsModule } from '@angular/forms';
import { BrowserModule } from '@angular/platform-browser';
import { BrowserAnimationsModule } from '@angular/platform-browser/animations';
import { ToastrModule } from 'ngx-toastr';

import { AppRoutingModule } from './app-routing.module';
import { AppComponent } from './app.component';
import { EditComponent } from './components/edit/edit.component';
import { NetworkEditComponent } from './components/network-edit/network.edit.component';
import { HomeComponent } from './components/home/home.component';
import { LoadingComponent } from './components/loading/loading.component';
import { LogsComponent } from './components/logs/logs.component';
import { NetworkComponent } from './components/network/network.component';
import { SettingsComponent } from './components/settings/settings.component';
import { SwarmComponent } from './components/swarm/swarm.component';
import { ThemeConfigComponent } from './components/settings/theme-config.component';
import { AppLayoutModule } from './layout/app.layout.module';
import { ANSIPipe } from './pipes/ansi.pipe';
import { DateAgoPipe } from './pipes/date-ago.pipe';
import { HashSuffixPipe } from './pipes/hash-suffix.pipe';
import { PrimeNGModule } from './prime-ng.module';
import { MessageModule } from 'primeng/message';
import { TooltipModule } from 'primeng/tooltip';

const components = [
  AppComponent,
  EditComponent,
  NetworkEditComponent,
  HomeComponent,
  LoadingComponent,
  NetworkComponent,
  SettingsComponent,
  LogsComponent
];

@NgModule({
  declarations: [
    ...components,

    ANSIPipe,
    DateAgoPipe,
    SwarmComponent,
    SettingsComponent,
    HashSuffixPipe,
    ThemeConfigComponent
  ],
  imports: [
    BrowserModule,
    AppRoutingModule,
    HttpClientModule,
    ReactiveFormsModule,
    FormsModule,
    ToastrModule.forRoot({
      positionClass: 'toast-bottom-right'
    }),
    BrowserAnimationsModule,
    CommonModule,
    PrimeNGModule,
    AppLayoutModule,
    MessageModule,
    TooltipModule
  ],
  providers: [
    { provide: LocationStrategy, useClass: HashLocationStrategy },
  ],
  bootstrap: [AppComponent]
})
export class AppModule { }
