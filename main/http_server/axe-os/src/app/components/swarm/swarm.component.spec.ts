import { ComponentFixture, TestBed } from '@angular/core/testing';
import { SwarmComponent } from './swarm.component';
import { HttpClientTestingModule } from '@angular/common/http/testing';
import { CUSTOM_ELEMENTS_SCHEMA } from '@angular/core';
import { ToastrModule } from 'ngx-toastr';
import { HashSuffixPipe } from '../../pipes/hash-suffix.pipe';
import { ReactiveFormsModule, FormsModule } from '@angular/forms';
import { SliderModule } from 'primeng/slider';
import { InputTextModule } from 'primeng/inputtext';
import { ButtonModule } from 'primeng/button';

describe('SwarmComponent', () => {
  let component: SwarmComponent;
  let fixture: ComponentFixture<SwarmComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [
        HttpClientTestingModule,
        ToastrModule.forRoot(),
        ReactiveFormsModule,
        FormsModule,
        SliderModule,
        InputTextModule,
        ButtonModule
      ],
      declarations: [SwarmComponent, HashSuffixPipe],
      schemas: [CUSTOM_ELEMENTS_SCHEMA]
    });
    fixture = TestBed.createComponent(SwarmComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
