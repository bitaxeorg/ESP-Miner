import { Component } from '@angular/core';
import { interval, Observable, shareReplay, startWith, switchMap } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';
import { LoadingService } from 'src/app/services/loading.service';
import { ISystemInfo } from 'src/models/ISystemInfo';
import { ISystemASIC } from 'src/models/ISystemASIC';

@Component({
  selector: 'app-system',
  templateUrl: './system.component.html',
})
export class SystemComponent {
  public info$: Observable<ISystemInfo>;
  public asic$: Observable<ISystemASIC>;

  constructor(
    private systemService: SystemService,
    private loadingService: LoadingService,
  ) {
    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => this.systemService.getInfo()),
      shareReplay({ refCount: true, bufferSize: 1 })
    );

    this.asic$ = this.systemService.getAsicSettings().pipe(
      shareReplay({refCount: true, bufferSize: 1})
    );

    this.info$.subscribe({
      next: () => {
        this.loadingService.loading$.next(false)
      }
    });
  }

  ngOnInit() {
    this.loadingService.loading$.next(true);
  }
}
