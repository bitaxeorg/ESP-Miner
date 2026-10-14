import { ComponentFixture, TestBed } from '@angular/core/testing';
import { provideHttpClient } from '@angular/common/http';
import { provideRouter } from '@angular/router';
import { CommonModule } from '@angular/common';
import { of } from 'rxjs';

import { SoloChanceComponent } from './solo-chance.component';
import { LiveDataService } from 'src/app/services/live-data.service';
import { SystemApiService } from 'src/app/services/system.service';
import { LoadingService } from 'src/app/services/loading.service';
import { TooltipDirective } from 'src/app/directives/tooltip.directive';
import { DiffSuffixPipe } from 'src/app/pipes/diff-suffix.pipe';
import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { TooltipIconComponent } from '../tooltip-icon/tooltip-icon.component';
import { TooltipTextIconComponent } from '../tooltip-text-icon/tooltip-text-icon.component';

const mockLiveDataService = {
  info$: of({
    expectedHashrate: 500,
    networkDifficulty: 1e12,
    poolDifficulty: 1000,
    bestSessionDiff: 5000,
    bestDiff: 10000,
    uptimeSeconds: 3600,
    totalUptimeSeconds: 7200
  })
};

describe('SoloChanceComponent', () => {
  let component: SoloChanceComponent;
  let fixture: ComponentFixture<SoloChanceComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [SoloChanceComponent, TooltipIconComponent, TooltipTextIconComponent],
      imports: [CommonModule, TooltipDirective, DiffSuffixPipe, HashSuffixPipe],
      providers: [
        provideRouter([]),
        provideHttpClient(),
        SystemApiService,
        LoadingService,
        { provide: LiveDataService, useValue: mockLiveDataService }
      ]
    }).compileComponents();

    fixture = TestBed.createComponent(SoloChanceComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
