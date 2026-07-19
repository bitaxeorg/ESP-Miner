import { ComponentFixture, TestBed } from '@angular/core/testing';
import { Router, provideRouter } from '@angular/router';

import { NetworkComponent } from './network.component';
import { NetworkEditComponent } from '../network-edit/network.edit.component';
import { SecurityComponent } from '../security/security.component';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { provideAnimations } from '@angular/platform-browser/animations';
import { DialogService } from 'src/app/services/dialog.service';

describe('NetworkComponent', () => {
  let component: NetworkComponent;
  let fixture: ComponentFixture<NetworkComponent>;

  // Render the component as if the router were at the given URL. isAPMode is
  // derived from router.url, so stubbing the getter controls AP vs normal mode.
  function createAt(url: string): void {
    spyOnProperty(TestBed.inject(Router), 'url', 'get').and.returnValue(url);
    fixture = TestBed.createComponent(NetworkComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  }

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [NetworkComponent, NetworkEditComponent],
      imports: [SecurityComponent],
      providers: [provideHttpClient(), provideToastr(), provideAnimations(), provideRouter([]), DialogService]
    });
  });

  it('should create', () => {
    createAt('/network');
    expect(component).toBeTruthy();
  });

  it('does not show the security panel on the normal network page', () => {
    createAt('/network');
    expect(component.isAPMode).toBeFalse();
    expect(fixture.nativeElement.querySelector('app-security')).toBeNull();
  });

  it('surfaces the security panel in AP (recovery) mode so a forgotten password can be reset', () => {
    createAt('/ap');
    expect(component.isAPMode).toBeTrue();
    expect(fixture.nativeElement.querySelector('app-security')).not.toBeNull();
  });
});
