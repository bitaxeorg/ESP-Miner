import { AfterViewChecked, Component, Input, OnInit, ElementRef, OnDestroy, ViewChild, HostListener } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { Subscription } from 'rxjs';
import { ToastrService } from 'ngx-toastr';
import { WebsocketService } from 'src/app/services/web-socket.service';

@Component({
  selector: 'app-logs',
  templateUrl: './logs.component.html',
  styleUrl: './logs.component.scss'
})
export class LogsComponent implements OnInit, OnDestroy, AfterViewChecked {

  public form!: FormGroup;

  public logs: { className: string, text: string }[] = [];

  private websocketSubscription?: Subscription;

  public stopScroll: boolean = false;

  public isExpanded: boolean = false;

  @ViewChild('scrollContainer') private scrollContainer!: ElementRef;

  @HostListener('document:keydown.esc', ['$event'])
  onEscKey() {
    if (this.isExpanded) {
      this.isExpanded = false;
    }
  }

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private websocketService: WebsocketService,
    private toastr: ToastrService,
  ) {}

  ngOnInit(): void {
    this.subscribeLogs();

    this.form = this.fb.group({
      filter: ["", [Validators.required]]
    });
  }

  ngOnDestroy(): void {
    this.websocketSubscription?.unsubscribe();
    this.clearLogs();
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
            this.logs.push({ className: `max-w-full text-monospace ${className}`, text: val });
          }

          if (this.logs.length > 256) {
            this.logs.shift();
          }
        },
        error: (error) => {
          this.toastr.error("Error opening websocket connection");
        }
      })
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
