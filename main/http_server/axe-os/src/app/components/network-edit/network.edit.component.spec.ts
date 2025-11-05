import { ComponentFixture, TestBed } from '@angular/core/testing';
import { NetworkEditComponent } from './network.edit.component';
import { HttpClientTestingModule } from '@angular/common/http/testing';
import { CUSTOM_ELEMENTS_SCHEMA } from '@angular/core';
import { ToastrModule } from 'ngx-toastr';
import { DialogService } from '../../services/dialog.service';
import { DialogService as PrimeDialogService } from 'primeng/dynamicdialog';

describe('NetworkEditComponent', () => {
  let component: NetworkEditComponent;
  let fixture: ComponentFixture<NetworkEditComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [
        HttpClientTestingModule,
        ToastrModule.forRoot()
      ],
      declarations: [NetworkEditComponent],
      providers: [DialogService, PrimeDialogService],
      schemas: [CUSTOM_ELEMENTS_SCHEMA]
    });
    fixture = TestBed.createComponent(NetworkEditComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
