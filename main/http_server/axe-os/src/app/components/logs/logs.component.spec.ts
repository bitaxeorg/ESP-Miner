import { provideRouter } from '@angular/router';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { SystemApiService } from 'src/app/services/system.service';
import { LogsComponent } from './logs.component';
import { ComponentFixture, TestBed } from '@angular/core/testing';

describe('LogsComponent', () => {
  let component: LogsComponent;
  let fixture: ComponentFixture<LogsComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [
        LogsComponent
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
