import { provideRouter } from '@angular/router';
import { ANSIPipe } from 'src/app/pipes/ansi.pipe';
import { CommonModule } from '@angular/common';
import { SystemApiService } from 'src/app/services/system.service';
import { provideHttpClient } from '@angular/common/http';
import { TooltipDirective } from '../../directives/tooltip.directive';
import { ReactiveFormsModule } from '@angular/forms';
import { provideToastr } from 'ngx-toastr';
import { LogsComponent } from './logs.component';
import { ComponentFixture, TestBed } from '@angular/core/testing';

describe('LogsComponent', () => {
  let component: LogsComponent;
  let fixture: ComponentFixture<LogsComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      declarations: [LogsComponent],
      imports: [
        CommonModule,
        ReactiveFormsModule,
        TooltipDirective,
        ANSIPipe
      ],
      providers: [
        provideRouter([]),
        provideToastr(),
        provideHttpClient(),
        SystemApiService
      ]
    })
    .compileComponents();
    
    fixture = TestBed.createComponent(LogsComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
