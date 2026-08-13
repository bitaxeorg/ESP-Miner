import 'chartjs-adapter-moment';
import { ApplicationConfig, importProvidersFrom } from '@angular/core';
import { provideRouter, withHashLocation } from '@angular/router';
import { provideHttpClient } from '@angular/common/http';
import { provideAnimations } from '@angular/platform-browser/animations';
import { ToastrModule } from 'ngx-toastr';

import { routes } from './app.routes';
import { ApiConfiguration } from './generated/api-configuration';
import { Api } from './generated/api';
import { DialogService } from './services/dialog.service';

export const appConfig: ApplicationConfig = {
  providers: [
    provideRouter(routes, withHashLocation()),
    provideHttpClient(),
    provideAnimations(),
    importProvidersFrom(
      ToastrModule.forRoot({
        positionClass: 'toast-bottom-right'
      })
    ),
    { provide: ApiConfiguration, useValue: { rootUrl: '' } },
    Api,
    DialogService
  ]
};
