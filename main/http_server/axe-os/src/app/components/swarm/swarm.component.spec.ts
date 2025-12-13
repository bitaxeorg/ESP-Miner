import { ComponentFixture, TestBed } from '@angular/core/testing';

import { SwarmComponent } from './swarm.component';
import { ModalComponent } from '../modal/modal.component';
import { FileUploadModule } from 'primeng/fileupload';
import { InputGroupModule } from 'primeng/inputgroup';
import { ReactiveFormsModule } from '@angular/forms';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';

describe('SwarmComponent', () => {
  let component: SwarmComponent;
  let fixture: ComponentFixture<SwarmComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [SwarmComponent, ModalComponent],
      imports: [FileUploadModule, InputGroupModule, ReactiveFormsModule],
      providers: [provideHttpClient(), provideToastr()]
    });
    fixture = TestBed.createComponent(SwarmComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
