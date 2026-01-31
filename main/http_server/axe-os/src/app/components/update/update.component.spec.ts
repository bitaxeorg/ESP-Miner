import { ComponentFixture, TestBed } from '@angular/core/testing';

import { UpdateComponent } from './update.component';
import { ModalComponent } from '../modal/modal.component';
import { FileUploadModule } from 'primeng/fileupload';
import { CheckboxModule } from 'primeng/checkbox';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';

describe('UpdateComponent', () => {
  let component: UpdateComponent;
  let fixture: ComponentFixture<UpdateComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      declarations: [UpdateComponent, ModalComponent],
      imports: [FileUploadModule, CheckboxModule],
      providers: [provideHttpClient(), provideToastr()]
    });
    fixture = TestBed.createComponent(UpdateComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
