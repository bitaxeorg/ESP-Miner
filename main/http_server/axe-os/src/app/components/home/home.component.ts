import { AfterViewChecked, Component, ElementRef, OnDestroy, ViewChild } from '@angular/core';
import { ToastrService } from 'ngx-toastr';
import { interval, Observable, shareReplay, startWith, Subscription, switchMap } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';
import { WebsocketService } from 'src/app/services/web-socket.service';

@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent implements AfterViewChecked, OnDestroy {
  @ViewChild('scrollContainer') private scrollContainer!: ElementRef;

  public info$: Observable<any>;

  public logs: string[] = [];

  private websocketSubscription: Subscription;


  constructor(
    private systemService: SystemService,
    private toastr: ToastrService,
    private loadingService: LoadingService,
    private websocketService: WebsocketService
  ) {

    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      shareReplay({ refCount: true, bufferSize: 1 })
    )

    this.websocketSubscription = this.websocketService.ws$.subscribe({
      next: (val) => {
        this.logs.push(val);
        if (this.logs.length > 100) {
          this.logs.shift();
        }

      }
    })

  }
  ngOnDestroy(): void {
    this.websocketSubscription.unsubscribe();
  }
  ngAfterViewChecked(): void {
    if (this.scrollContainer?.nativeElement != null) {
      this.scrollContainer.nativeElement.scrollTo({ left: 0, top: this.scrollContainer.nativeElement.scrollHeight, behavior: 'smooth' });
    }
  }

  public restart() {
    this.systemService.restart().subscribe(res => {

    });
    this.toastr.success('Success!', 'Bitaxe restarted');
  }
}

