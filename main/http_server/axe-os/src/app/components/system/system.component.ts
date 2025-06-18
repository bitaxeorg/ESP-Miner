import { Component } from '@angular/core';
import { interval, map, Observable, shareReplay, startWith, switchMap } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';
import { ISystemInfo } from 'src/models/ISystemInfo';

@Component({
  selector: 'app-system',
  templateUrl: './system.component.html',
})
export class SystemComponent {
  public info$: Observable<ISystemInfo>;

  constructor(
    private systemService: SystemService
  ) {
    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => this.systemService.getInfo()),
      shareReplay({ refCount: true, bufferSize: 1 })
    );
}}
