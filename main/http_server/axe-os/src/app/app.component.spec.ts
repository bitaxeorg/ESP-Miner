import { TestBed } from '@angular/core/testing';
import { AppComponent } from './app.component';
import { SnowflakesComponent } from './components/snowflakes/snowflakes.component';
import { provideRouter, RouterModule } from '@angular/router';
import { LayoutService } from './layout/service/app.layout.service';
import { ThemeService } from './services/theme.service';
import { LocalStorageService } from './local-storage.service';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';

describe('AppComponent', () => {
  beforeEach(() => TestBed.configureTestingModule({
    imports: [RouterModule],
    declarations: [AppComponent, SnowflakesComponent],
    providers: [
      provideRouter([]),
      LayoutService,
      ThemeService,
      LocalStorageService,
      provideHttpClient(),
      provideToastr()
    ]
  }));

  it('should create the app', () => {
    const fixture = TestBed.createComponent(AppComponent);
    const app = fixture.componentInstance;
    expect(app).toBeTruthy();
  });
});
