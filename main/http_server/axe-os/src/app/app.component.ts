import { Component, OnDestroy } from '@angular/core';
import { Router, TitleStrategy } from '@angular/router';
import { Subject } from 'rxjs';
import { takeUntil } from 'rxjs/operators';

import { I18nService } from './i18n/i18n.service';

@Component({
  selector: 'app-root',
  templateUrl: './app.component.html',
  styleUrls: ['./app.component.scss']
})
export class AppComponent implements OnDestroy {
  private destroy$ = new Subject<void>();

  constructor(
    private router: Router,
    private titleStrategy: TitleStrategy,
    private i18n: I18nService
  ) {
    this.i18n.locale$
      .pipe(takeUntil(this.destroy$))
      .subscribe(() => {
        this.titleStrategy.updateTitle(this.router.routerState.snapshot);
      });
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }
}
