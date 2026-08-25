import { Injectable } from '@angular/core';
import { BehaviorSubject, Observable, Subject, EMPTY, timer, merge, fromEvent } from 'rxjs';
import { catchError, retry, share, tap, switchMap, startWith, scan, shareReplay, map, timeout, bufferTime, filter, distinctUntilChanged, exhaustMap } from 'rxjs/operators';
import { webSocket, WebSocketSubject } from 'rxjs/webSocket';
import { SystemInfo as ISystemInfo } from 'src/app/generated/models';
import { SystemApiService } from './system.service';
import { environment } from 'src/environments/environment';

@Injectable({
  providedIn: 'root'
})
export class LiveDataService {
  private socket$: WebSocketSubject<any> | null = null;
  private updates$ = new Subject<Partial<ISystemInfo>>();

  // Connection diagnostics, read only by the close log below.
  private openedAt = 0;
  private lastMessageAt = 0;

  // Shared info stream for the whole app
  public readonly info$: Observable<ISystemInfo>;

  // Connection status for the UI
  private connectedSubject = new BehaviorSubject<boolean>(false);
  public connected$ = this.connectedSubject.asObservable();

  constructor(
    private systemService: SystemApiService
  ) {
    // Visibility stream for polling adjustments
    const visibility$ = fromEvent(document, 'visibilitychange').pipe(
      map(() => document.visibilityState),
      startWith(document.visibilityState),
      distinctUntilChanged(),
      shareReplay(1)
    );

    // Periodic polling fallback (adjust frequency based on visibility)
    const fallbackPolling$ = visibility$.pipe(
      switchMap(state => {
        const interval = state === 'visible' ? 3000 : 60000; // 3s when visible, 60s when hidden
        return timer(0, interval).pipe(
          exhaustMap(() => {
            // Only poll if not connected OR if backgrounded (to keep data fresh)
            if (this.connectedSubject.value && state === 'visible') return EMPTY;
            return this.systemService.getInfo().pipe(
              tap(() => {
                if (!this.connectedSubject.value) {
                  this.connectedSubject.next(true);
                }
              }),
              catchError(() => EMPTY)
            );
          })
        );
      }),
      catchError(() => EMPTY)
    );

    const updates$ = merge(
      this.connect().pipe(switchMap(() => EMPTY), catchError(() => EMPTY)),
      this.updates$.pipe(
        // Buffer updates to handle bursts when tab is resumed (1000ms = 1Hz UI refresh)
        bufferTime(1000),
        filter(msgs => msgs.length > 0),
        map(msgs => msgs.reduce((acc, curr) => ({ ...acc, ...curr }), {} as Partial<ISystemInfo>))
      ),
      fallbackPolling$
    );

    const initialInfo$ = this.systemService.getInfo().pipe(
      catchError(err => {
        console.error('Initial info fetch failed', err);
        return EMPTY;
      })
    );

    this.info$ = merge(initialInfo$, updates$).pipe(
      scan((acc: ISystemInfo, curr: Partial<ISystemInfo>) => ({ ...acc, ...curr } as ISystemInfo), {} as ISystemInfo),
      // Ensure we have at least once received a message with a recognizable field before emitting
      filter(info => !!info.version || !!info.uptimeSeconds),
      shareReplay(1)
    );
  }

  private connect(): Observable<any> {
    if (environment.mock || this.socket$ || !window.location.host) {
      return EMPTY;
    }

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = window.location.host;
    const url = `${protocol}//${host}/api/ws/live`;

    this.socket$ = webSocket({
      url,
      openObserver: {
        next: () => {
          this.openedAt = Date.now();
          this.lastMessageAt = 0;
          console.log('Live WebSocket connected');
          this.connectedSubject.next(true);
        }
      },
      closeObserver: {
        next: (event: CloseEvent) => {
          console.log(
            `Live WebSocket disconnected (code=${event?.code} wasClean=${event?.wasClean}` +
            ` afterMs=${this.openedAt ? Date.now() - this.openedAt : -1}` +
            ` lastMsgAgoMs=${this.lastMessageAt ? Date.now() - this.lastMessageAt : -1})`
          );
          this.connectedSubject.next(false);
          this.socket$ = null;
        }
      }
    });

    return this.socket$.pipe(
      timeout(10000),
      tap(msg => {
        this.lastMessageAt = Date.now();
        if (msg.event === 'update' && msg.data) {
          this.updates$.next(msg.data);
        }
      }),
      retry({ delay: 5000 }),
      share({ resetOnRefCountZero: false })
    );
  }
}
