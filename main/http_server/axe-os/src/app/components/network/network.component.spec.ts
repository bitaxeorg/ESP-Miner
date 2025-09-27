import { ComponentFixture, TestBed } from '@angular/core/testing';
import { NetworkComponent } from './network.component';
import { NetworkEditComponent } from '../network-edit/network.edit.component';
import { HttpClientTestingModule } from '@angular/common/http/testing';
import { CUSTOM_ELEMENTS_SCHEMA } from '@angular/core';
import { ToastrModule } from 'ngx-toastr';
import { DialogService } from '../../services/dialog.service';
import { DialogService as PrimeDialogService } from 'primeng/dynamicdialog';

describe('NetworkComponent', () => {
  let component: NetworkComponent;
  let fixture: ComponentFixture<NetworkComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [
        HttpClientTestingModule,
        ToastrModule.forRoot()
      ],
      declarations: [NetworkComponent, NetworkEditComponent],
      providers: [DialogService, PrimeDialogService],
      schemas: [CUSTOM_ELEMENTS_SCHEMA]
    });
    fixture = TestBed.createComponent(NetworkComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
