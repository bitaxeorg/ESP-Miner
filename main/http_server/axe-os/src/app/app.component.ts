import { Component, OnDestroy } from '@angular/core';
import { Router, TitleStrategy } from '@angular/router';
import { Subject, takeUntil } from 'rxjs';
import { LayoutService } from './layout/service/app.layout.service';
import { I18nService } from './i18n/i18n.service';

@Component({
    selector: 'app-root',
    templateUrl: './app.component.html',
    styleUrls: ['./app.component.scss'],
    standalone: false
})
export class AppComponent implements OnDestroy {
  private destroy$ = new Subject<void>();

  constructor(
    public layoutService: LayoutService,
    private router: Router,
    private titleStrategy: TitleStrategy,
    private i18n: I18nService,
  ) {
    this.i18n.locale$
      .pipe(takeUntil(this.destroy$))
      .subscribe(() => this.titleStrategy.updateTitle(this.router.routerState.snapshot));
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }
}
