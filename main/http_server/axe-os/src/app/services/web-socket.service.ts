import { Injectable } from '@angular/core';
import { webSocket, WebSocketSubject } from 'rxjs/webSocket';

@Injectable({
  providedIn: 'root'
})
export class WebsocketService {

  public ws$: WebSocketSubject<string>;

  constructor() {
    this.ws$ = webSocket({
      url: `ws://${window.location.host}/api/ws`,
      deserializer: (e: MessageEvent) => { return e.data }
    });
  }
}
