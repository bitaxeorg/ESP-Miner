import { provideRouter } from '@angular/router';
import { DateAgoPipe } from 'src/app/pipes/date-ago.pipe';
import { DiffSuffixPipe } from 'src/app/pipes/diff-suffix.pipe';
import { TooltipModule } from 'primeng/tooltip';
import { CommonModule } from '@angular/common';
import { LocalStorageService } from 'src/app/local-storage.service';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemApiService } from 'src/app/services/system.service';
import { provideHttpClient } from '@angular/common/http';
import { ScoreboardComponent } from './scoreboard.component';
import { ComponentFixture, TestBed } from '@angular/core/testing';

describe('ScoreboardComponent', () => {
  let component: ScoreboardComponent;
  let fixture: ComponentFixture<ScoreboardComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [ScoreboardComponent],
      imports: [CommonModule, TooltipModule, DiffSuffixPipe, DateAgoPipe],
      providers: [
        provideRouter([]),
        provideHttpClient(),
        SystemApiService,
        LoadingService,
        LocalStorageService
      ]
    })
    .compileComponents();

    fixture = TestBed.createComponent(ScoreboardComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
