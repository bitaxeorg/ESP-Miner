import { AfterViewChecked, Component, ElementRef, ViewChild } from '@angular/core';
import { ToastrService } from 'ngx-toastr';
import { Observable } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';
import { WebsocketService } from 'src/app/services/web-socket.service';

@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent implements AfterViewChecked {
  @ViewChild('scrollContainer') private scrollContainer!: ElementRef;

  public info$: Observable<any>;

  public logs: string[] = [];

  constructor(
    private systemService: SystemService,
    private toastr: ToastrService,
    private loadingService: LoadingService,
    private websocketService: WebsocketService
  ) {
    this.info$ = this.systemService.getInfo().pipe(
      this.loadingService.lockUIUntilComplete()
    )

    this.websocketService.ws$.subscribe({
      next: (val) => {
        this.logs.push(val);
        if (this.logs.length > 100) {
          this.logs.shift();
        }

      }
    })

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

