import { AsyncPipe } from '@angular/common';
import { Component, NO_ERRORS_SCHEMA } from '@angular/core';
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { provideRouter, Router, RouterLink, RouterOutlet } from '@angular/router';
import { BehaviorSubject, of, Subject } from 'rxjs';
import { SystemInfo } from '../generated/models';
import { LiveDataService } from '../services/live-data.service';
import { SensitiveData } from '../services/sensitive-data.service';
import { AppLayoutComponent } from './app.layout.component';
import { LayoutService } from './service/app.layout.service';

@Component({ template: '' })
class TestUpdatePage { }

describe('AppLayoutComponent update protection warning', () => {
  let info: BehaviorSubject<Partial<SystemInfo>>;
  let fixture: ComponentFixture<AppLayoutComponent>;

  beforeEach(() => {
    info = new BehaviorSubject<Partial<SystemInfo>>({ otaButtonRequired: 1 });
    TestBed.configureTestingModule({
      imports: [AppLayoutComponent],
      providers: [
        provideRouter([{ path: 'update', component: TestUpdatePage }]),
        { provide: LayoutService, useValue: {
          config: () => ({ colorScheme: 'dark', menuMode: 'static' }),
          state: {},
          overlayOpen$: new Subject<void>()
        } },
        { provide: LiveDataService, useValue: { info$: info.asObservable() } },
        { provide: SensitiveData, useValue: { hidden: of(false) } }
      ]
    }).overrideComponent(AppLayoutComponent, {
      set: { imports: [AsyncPipe, RouterLink, RouterOutlet], schemas: [NO_ERRORS_SCHEMA] }
    });
    fixture = TestBed.createComponent(AppLayoutComponent);
    fixture.detectChanges();
  });

  it('does not warn when protection is enabled or its state is not yet known', () => {
    expect(fixture.nativeElement.querySelector('[role="alert"]')).toBeNull();
    info.next({});
    fixture.detectChanges();
    expect(fixture.nativeElement.querySelector('[role="alert"]')).toBeNull();
  });

  it('keeps the warning across navigation and removes it when protection is restored', async () => {
    info.next({ otaButtonRequired: 0 });
    fixture.detectChanges();
    const warning: HTMLElement = fixture.nativeElement.querySelector('[role="alert"]');
    expect(warning.textContent).toContain('Button protection is off');
    const link = warning.querySelector('a')!;
    link.click();
    await fixture.whenStable();
    fixture.detectChanges();
    expect(TestBed.inject(Router).url).toBe('/update');
    expect(fixture.nativeElement.querySelector('[role="alert"]')).not.toBeNull();
    info.next({ otaButtonRequired: 1 });
    fixture.detectChanges();
    expect(fixture.nativeElement.querySelector('[role="alert"]')).toBeNull();
  });
});
