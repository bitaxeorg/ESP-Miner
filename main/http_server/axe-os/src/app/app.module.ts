import 'chartjs-adapter-moment';

import { CommonModule, HashLocationStrategy, LocationStrategy } from '@angular/common';
import { HttpClientModule } from '@angular/common/http';
import { NgModule, CUSTOM_ELEMENTS_SCHEMA, NO_ERRORS_SCHEMA } from '@angular/core';
import { FormsModule, ReactiveFormsModule } from '@angular/forms';
import { BrowserModule } from '@angular/platform-browser';
import { BrowserAnimationsModule } from '@angular/platform-browser/animations';
import { ToastrModule } from 'ngx-toastr';

import { AppRoutingModule } from './app-routing.module';
import { AppComponent } from './app.component';
import { EditComponent } from './components/edit/edit.component';
import { PoolComponent } from './components/pool/pool.component';
import { NetworkEditComponent } from './components/network-edit/network.edit.component';
import { HomeComponent } from './components/home/home.component';
import { LoadingComponent } from './components/loading/loading.component';
import { LogsComponent } from './components/logs/logs.component';
import { NetworkComponent } from './components/network/network.component';
import { SettingsComponent } from './components/settings/settings.component';
import { SwarmComponent } from './components/swarm/swarm.component';
import { ThemeConfigComponent } from './components/design/theme-config.component';
import { DesignComponent } from './components/design/design.component';
import { AppLayoutModule } from './layout/app.layout.module';
import { ANSIPipe } from './pipes/ansi.pipe';
import { DateAgoPipe } from './pipes/date-ago.pipe';
import { HashSuffixPipe } from './pipes/hash-suffix.pipe';
import { PrimeNGModule } from './prime-ng.module';
import { MessageModule } from 'primeng/message';
import { TooltipModule } from 'primeng/tooltip';
import { DialogModule } from 'primeng/dialog';
import { DynamicDialogModule, DialogService as PrimeDialogService } from 'primeng/dynamicdialog';
import { DialogService, DialogListComponent } from './services/dialog.service';

import { ApplicationConfig } from '@angular/core';
import { provideAnimationsAsync } from '@angular/platform-browser/animations/async';
import { providePrimeNG } from 'primeng/config';
import Aura from '@primeng/themes/aura';

const components = [
  AppComponent,
  EditComponent,
  NetworkEditComponent,
  HomeComponent,
  LoadingComponent,
  NetworkComponent,
  SettingsComponent,
  LogsComponent,
  PoolComponent
];

@NgModule({
  declarations: [
    ...components,

    ANSIPipe,
    DateAgoPipe,
    SwarmComponent,
    SettingsComponent,
    HashSuffixPipe,
    ThemeConfigComponent,
    DesignComponent,
    PoolComponent,
    DialogListComponent
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
    TooltipModule,
    DialogModule,
    DynamicDialogModule
  ],
  providers: [
    { provide: LocationStrategy, useClass: HashLocationStrategy },
    DialogService,
    PrimeDialogService,
    provideAnimationsAsync(),
    providePrimeNG({
            theme: {
                preset: Aura
            }
        })
  ],
  bootstrap: [AppComponent],
  schemas: [
    CUSTOM_ELEMENTS_SCHEMA,
    NO_ERRORS_SCHEMA
  ]
})
export class AppModule { }
