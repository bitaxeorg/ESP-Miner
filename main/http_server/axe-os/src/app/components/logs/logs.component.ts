import { AfterViewChecked, Component, Input, OnInit, ElementRef, OnDestroy, ViewChild, HostListener } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { Subscription } from 'rxjs';
import { ToastrService } from 'ngx-toastr';
import { WebsocketService } from 'src/app/services/web-socket.service';
import { SystemApiService } from 'src/app/services/system.service';

@Component({
  selector: 'app-logs',
  templateUrl: './logs.component.html',
  styleUrl: './logs.component.scss'
})
export class LogsComponent implements OnInit, OnDestroy, AfterViewChecked {
  public loadingLogs: boolean = false;

  public form!: FormGroup;

  public logs: { className: string, text: string }[] = [];

  private websocketSubscription?: Subscription;

  public stopScroll: boolean = false;

  @ViewChild('scrollContainer') private scrollContainer!: ElementRef;


  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private websocketService: WebsocketService,
    private systemApiService: SystemApiService,
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

  private logBuffer: string = '';

  private subscribeLogs() {
    this.websocketSubscription = this.websocketService.ws$.subscribe({
        next: (val) => {
          this.logBuffer += val;

          // Only process when we have a complete line (ending with newline)
          if (this.logBuffer.endsWith('\n')) {
            const completeLine = this.logBuffer;
            this.logBuffer = '';

            let className = '';
            if (completeLine.length > 0) {
                const prefix = completeLine[0];
                switch(prefix) {
                    case 'E':
                    case 'W':
                    case 'I':
                        className = `log-${prefix}`;
                        break;
                }
            }

            // Get current filter value from form
            const currentFilter = this.form?.get('filter')?.value;

            if (!currentFilter || completeLine.includes(currentFilter)) {
              this.logs.push({ className: `max-w-full text-monospace ${className}`, text: completeLine });
            }

            if (this.logs.length > 256) {
              this.logs.shift();
            }
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

  public downloadLogs() {
    this.loadingLogs = true;
    this.systemApiService.downloadLogs(this.uri).subscribe({
      next: (blob) => {
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.style.display = 'none';
        a.href = url;
        a.download = 'bitaxe-logs.txt';
        document.body.appendChild(a);
        a.click();
        window.URL.revokeObjectURL(url);
        this.toastr.success("Logs downloaded successfully");
        this.loadingLogs = false;
      },
      error: (error) => {
        console.error('There was a problem with the log download:', error);
        this.toastr.error("Failed to download logs");
        this.loadingLogs = false;
      }
    });
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
