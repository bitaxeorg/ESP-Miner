import { ComponentFixture, TestBed } from '@angular/core/testing';

import { NetworkComponent } from './network.component';
import { NetworkEditComponent } from '../network-edit/network.edit.component';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { DialogService } from 'src/app/services/dialog.service';
import { DialogService as PrimeDialogService } from 'primeng/dynamicdialog';

describe('NetworkComponent', () => {
  let component: NetworkComponent;
  let fixture: ComponentFixture<NetworkComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [NetworkComponent, NetworkEditComponent],
      providers: [provideHttpClient(), provideToastr(), DialogService, PrimeDialogService]
    });
    fixture = TestBed.createComponent(NetworkComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
