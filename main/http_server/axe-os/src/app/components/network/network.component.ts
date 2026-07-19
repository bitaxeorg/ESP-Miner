import { Component, ViewChild, AfterViewInit } from '@angular/core';
import { FormGroup } from '@angular/forms';
import { Observable } from 'rxjs';
import { Router } from '@angular/router';
import { NetworkEditComponent } from '../network-edit/network.edit.component';

@Component({
    selector: 'app-network',
    templateUrl: './network.component.html',
    styleUrls: ['./network.component.scss'],
    standalone: false
})
export class NetworkComponent implements AfterViewInit {
  form$!: Observable<FormGroup | null>;

  @ViewChild(NetworkEditComponent) networkEditComponent!: NetworkEditComponent;

  constructor(private router: Router) {}

  // In AP (setup/recovery) mode ApModeGuard redirects every other page to this
  // one, so the settings-based password reset is unreachable. Surface it here so
  // a forgotten password can still be cleared. The '/ap' route is only ever used
  // while the device is in AP mode (mirrors AppLayoutComponent.isAPMode).
  get isAPMode(): boolean {
    return this.router.url.startsWith('/ap');
  }

  ngAfterViewInit() {
    this.form$ = this.networkEditComponent.form$;
  }
}
