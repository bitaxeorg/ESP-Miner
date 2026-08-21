import { TestBed } from '@angular/core/testing';
import { AppComponent } from './app.component';
import { SnowflakesComponent } from './components/snowflakes/snowflakes.component';
import { provideRouter, RouterModule } from '@angular/router';
import { LayoutService } from './layout/service/app.layout.service';
import { ThemeService } from './services/theme.service';
import { LocalStorageService } from './local-storage.service';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { DialogListComponent } from './services/dialog.service';
import { LoginModalComponent } from './components/login-modal/login-modal.component';
import { ModalComponent } from './components/modal/modal.component';
import { FormsModule } from '@angular/forms';
import { CommonModule } from '@angular/common';

describe('AppComponent', () => {
  beforeEach(() => TestBed.configureTestingModule({
    imports: [RouterModule, FormsModule, CommonModule],
    declarations: [AppComponent, SnowflakesComponent, DialogListComponent, LoginModalComponent, ModalComponent],
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
