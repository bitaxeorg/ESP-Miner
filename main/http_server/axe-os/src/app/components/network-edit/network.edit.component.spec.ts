import { ComponentFixture, TestBed } from '@angular/core/testing';

import { NetworkEditComponent } from './network.edit.component';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';
import { DialogService } from 'src/app/services/dialog.service';
import { DialogService as PrimeDialogService } from 'primeng/dynamicdialog';

describe('NetworkEditComponent', () => {
  let component: NetworkEditComponent;
  let fixture: ComponentFixture<NetworkEditComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [NetworkEditComponent],
      providers: [provideHttpClient(), provideToastr(), DialogService, PrimeDialogService]
    });
    fixture = TestBed.createComponent(NetworkEditComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
