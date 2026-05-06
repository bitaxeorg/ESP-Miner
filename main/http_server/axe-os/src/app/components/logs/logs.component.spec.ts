import { provideRouter } from '@angular/router';
import { ANSIPipe } from 'src/app/pipes/ansi.pipe';
import { InputTextModule } from 'primeng/inputtext';
import { CommonModule } from '@angular/common';
import { SystemApiService } from 'src/app/services/system.service';
import { provideHttpClient } from '@angular/common/http';
import { TooltipModule } from 'primeng/tooltip';
import { IconFieldModule } from 'primeng/iconfield';
import { InputIconModule } from 'primeng/inputicon';
import { ReactiveFormsModule } from '@angular/forms';
import { ButtonModule } from 'primeng/button';
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
        ButtonModule,
        ReactiveFormsModule,
        TooltipModule,
        InputTextModule,
        IconFieldModule,
        InputIconModule,
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
