import { ComponentFixture, TestBed } from '@angular/core/testing';
import { HomeComponent } from './home.component';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { ReactiveFormsModule, FormsModule } from '@angular/forms';
import { NoopAnimationsModule } from '@angular/platform-browser/animations';
import { Title } from '@angular/platform-browser';
import { provideRouter } from '@angular/router';
import { MessageModule } from 'primeng/message';
import { SelectModule } from 'primeng/select';
import { ChartModule } from 'primeng/chart';
import { ProgressBarModule } from 'primeng/progressbar';
import { TooltipModule } from 'primeng/tooltip';

import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { DiffSuffixPipe } from 'src/app/pipes/diff-suffix.pipe';
import { DateAgoPipe } from 'src/app/pipes/date-ago.pipe';
import { AddressPipe } from 'src/app/pipes/address.pipe';
import { SatsPipe } from 'src/app/pipes/sats.pipe';
import { ByteSuffixPipe } from 'src/app/pipes/byte-suffix.pipe';

import { TooltipTextIconComponent } from 'src/app/components/tooltip-text-icon/tooltip-text-icon.component';
import { ConfettiComponent } from 'src/app/components/confetti/confetti.component';
import { SnowflakesComponent } from 'src/app/components/snowflakes/snowflakes.component';

import { SystemApiService } from 'src/app/services/system.service';
import { ThemeService } from 'src/app/services/theme.service';
import { QuicklinkService } from 'src/app/services/quicklink.service';
import { LoadingService } from 'src/app/services/loading.service';
import { ShareRejectionExplanationService } from 'src/app/services/share-rejection-explanation.service';
import { LocalStorageService } from 'src/app/local-storage.service';
import { DashboardEditService } from 'src/app/services/dashboard-edit.service';
import { LayoutService } from 'src/app/layout/service/app.layout.service';

describe('HomeComponent', () => {
  let component: HomeComponent;
  let fixture: ComponentFixture<HomeComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [
        HomeComponent,
        TooltipTextIconComponent,
        ConfettiComponent,
        SnowflakesComponent
      ],
      imports: [
        ReactiveFormsModule,
        FormsModule,
        NoopAnimationsModule,
        MessageModule,
        SelectModule,
        ChartModule,
        ProgressBarModule,
        TooltipModule,
        HashSuffixPipe,
        DiffSuffixPipe,
        DateAgoPipe,
        AddressPipe,
        SatsPipe,
        ByteSuffixPipe
      ],
      providers: [
        provideRouter([]),
        provideHttpClient(),
        provideToastr(),
        SystemApiService,
        ThemeService,
        QuicklinkService,
        Title,
        LoadingService,
        ShareRejectionExplanationService,
        LocalStorageService,
        DashboardEditService,
        LayoutService
      ]
    });
    fixture = TestBed.createComponent(HomeComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  describe('stale data and visibility state', () => {
    it('should set stale data error when visible and last message is old', () => {
      spyOnProperty(document, 'visibilityState', 'get').and.returnValue('visible');

      component['lastMessageTime'] = Date.now() - 10000;
      component.systemInfoError$.next({ duration: 0, startTime: null });

      component['checkStaleData']();

      expect(component.systemInfoError$.value.duration).toBe(10);
    });

    it('should NOT set stale data error when hidden and last message is old', () => {
      spyOnProperty(document, 'visibilityState', 'get').and.returnValue('hidden');

      component['lastMessageTime'] = Date.now() - 10000;
      component.systemInfoError$.next({ duration: 0, startTime: null });

      component['checkStaleData']();

      expect(component.systemInfoError$.value.duration).toBe(0);
    });

    it('should reset lastMessageTime and clear stale error when transitioning to visible', () => {
      spyOnProperty(document, 'visibilityState', 'get').and.returnValue('visible');

      const initialTime = Date.now() - 10000;
      component['lastMessageTime'] = initialTime;
      component.systemInfoError$.next({ duration: 10, startTime: initialTime });

      component.onVisibilityChange();

      expect(component.systemInfoError$.value.duration).toBe(0);
      expect(component.systemInfoError$.value.startTime).toBeNull();
      expect(component['lastMessageTime']).toBeGreaterThan(initialTime);
    });
  });
});
