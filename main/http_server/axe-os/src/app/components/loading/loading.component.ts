import { Component } from '@angular/core';
import { combineLatest, Observable } from 'rxjs';
import { map } from 'rxjs/operators';
import { LoadingService } from 'src/app/services/loading.service';
import { AuthService } from 'src/app/services/auth.service';
import { AsyncPipe } from '@angular/common';

@Component({
  selector: 'app-loading',
  templateUrl: './loading.component.html',
  styleUrls: ['./loading.component.scss'],
  imports: [AsyncPipe]
})
export class LoadingComponent {

  public loading$: Observable<boolean>;

  constructor(
    private loadingService: LoadingService,
    private authService: AuthService,
  ) {
    this.loading$ = combineLatest([
      this.loadingService.loading$,
      this.authService.promptState$,
    ]).pipe(
      map(([loading, prompt]) => loading && !prompt)
    );
  }
}
