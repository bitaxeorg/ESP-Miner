import { AfterViewChecked, Component, ElementRef, OnDestroy, ViewChild } from '@angular/core';
import { interval, map, Observable, shareReplay, startWith, Subscription, switchMap } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';
import { WebsocketService } from 'src/app/services/web-socket.service';
import { ISystemInfo } from 'src/models/ISystemInfo';

@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent implements AfterViewChecked, OnDestroy {
  @ViewChild('scrollContainer') private scrollContainer!: ElementRef;

  public info$: Observable<ISystemInfo>;

  public quickLink$: Observable<string | undefined>;

  public logs: string[] = [];

  private websocketSubscription?: Subscription;

  public showLogs = false;


  constructor(
    private systemService: SystemService,
    private websocketService: WebsocketService
  ) {

    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      shareReplay({ refCount: true, bufferSize: 1 })
    );

    this.quickLink$ = this.info$.pipe(
      map(info => {
        if (info.stratumURL.includes('public-pool.io')) {
          const address = info.stratumUser.split('.')[0]
          return `https://web.public-pool.io/#/app/${address}`;
        } else {
          return undefined;
        }
      })
    )



  }
  ngOnDestroy(): void {
    this.websocketSubscription?.unsubscribe();
  }
  ngAfterViewChecked(): void {
    if (this.scrollContainer?.nativeElement != null) {
      this.scrollContainer.nativeElement.scrollTo({ left: 0, top: this.scrollContainer.nativeElement.scrollHeight, behavior: 'smooth' });
    }
  }

  public toggleLogs() {
    this.showLogs = !this.showLogs;

    if (this.showLogs) {
      this.websocketSubscription = this.websocketService.ws$.subscribe({
        next: (val) => {
          this.logs.push(val);
          if (this.logs.length > 100) {
            this.logs.shift();
          }
        }
      })
    } else {
      this.websocketSubscription?.unsubscribe();
    }
  }


}

