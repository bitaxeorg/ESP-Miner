import { AfterViewChecked, Component, Input, OnInit, ElementRef, OnDestroy, ViewChild, HostListener } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { interval, map, Observable, shareReplay, startWith, Subscription, switchMap } from 'rxjs';
import { SystemService } from 'src/app/services/system.service';
import { WebsocketService } from 'src/app/services/web-socket.service';
import { ISystemInfo } from 'src/models/ISystemInfo';

@Component({
  selector: 'app-logs',
  templateUrl: './logs.component.html',
  styleUrl: './logs.component.scss'
})
export class LogsComponent implements OnInit, OnDestroy, AfterViewChecked {

  public form!: FormGroup;

  @ViewChild('scrollContainer') private scrollContainer!: ElementRef;
  public info$: Observable<ISystemInfo>;

  public logs: { className: string, text: string }[] = [];

  private websocketSubscription?: Subscription;

  public showLogs = false;

  public stopScroll: boolean = false;

  public isExpanded: boolean = false;

  @HostListener('document:keydown.esc', ['$event'])
  onEscKey(event: KeyboardEvent) {
    if (this.isExpanded) {
      this.isExpanded = false;
    }
  }
  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private websocketService: WebsocketService,
    private systemService: SystemService
  ) {


    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      map(info => {
        info.power = parseFloat(info.power.toFixed(1))
        info.voltage = parseFloat((info.voltage / 1000).toFixed(1));
        info.current = parseFloat((info.current / 1000).toFixed(1));
        info.coreVoltageActual = parseFloat((info.coreVoltageActual / 1000).toFixed(2));
        info.coreVoltage = parseFloat((info.coreVoltage / 1000).toFixed(2));
        return info;
      }),
      shareReplay({ refCount: true, bufferSize: 1 })
    );


  }

  ngOnInit(): void {
    this.form = this.fb.group({
      filter: ["", [Validators.required]]
    });
  }

  ngOnDestroy(): void {
    this.websocketSubscription?.unsubscribe();
  }

  private subscribeLogs() {
    this.websocketSubscription = this.websocketService.ws$.subscribe({
        next: (val) => {
          const matches = val.matchAll(/\[(\d+;\d+)m(.*?)(?=\[|\n|$)/g);
          let className = 'ansi-white'; // default color

          for (const match of matches) {
            const colorCode = match[1].split(';')[1];
            switch (colorCode) {
              case '31': className = 'ansi-red'; break;
              case '32': className = 'ansi-green'; break;
              case '33': className = 'ansi-yellow'; break;
              case '34': className = 'ansi-blue'; break;
              case '35': className = 'ansi-magenta'; break;
              case '36': className = 'ansi-cyan'; break;
              case '37': className = 'ansi-white'; break;
            }
          }

          // Get current filter value from form
          const currentFilter = this.form?.get('filter')?.value;

          if (!currentFilter || val.includes(currentFilter)) {
            this.logs.push({ className, text: val });
          }

          if (this.logs.length > 256) {
            this.logs.shift();
          }
        }
      })
  }

  public toggleLogs() {
    this.showLogs = !this.showLogs;

    if (this.showLogs) {
      this.subscribeLogs();
    } else {
      this.websocketSubscription?.unsubscribe();
    }
  }

  public clearLogs() {
    this.logs.length = 0;
  }

  ngAfterViewChecked(): void {
    if(this.stopScroll == true){
      return;
    }
    if (this.scrollContainer?.nativeElement != null) {
      this.scrollContainer.nativeElement.scrollTo({ left: 0, top: this.scrollContainer.nativeElement.scrollHeight, behavior: 'smooth' });
    }
  }

}
