import { ComponentFixture, TestBed } from '@angular/core/testing';
import { DropdownComponent } from './dropdown.component';
import { SelectOption } from '../../models/select-option.model';
import { By } from '@angular/platform-browser';

describe('DropdownComponent', () => {
  let component: DropdownComponent;
  let fixture: ComponentFixture<DropdownComponent>;

  const options: SelectOption[] = [
    { label: 'Option 1', value: 1 },
    { label: 'Option 2', value: 2 },
    { label: 'Option 3', value: 3 }
  ];

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [DropdownComponent]
    }).compileComponents();

    fixture = TestBed.createComponent(DropdownComponent);
    component = fixture.componentInstance;
    component.options = options;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  it('should toggle open/close on trigger click', () => {
    const trigger = fixture.debugElement.query(By.css('.cursor-pointer'));
    
    // Initial state
    expect(component.isOpen).toBeFalse();

    // Click to open
    trigger.nativeElement.click();
    fixture.detectChanges();
    expect(component.isOpen).toBeTrue();

    // Click to close
    trigger.nativeElement.click();
    fixture.detectChanges();
    expect(component.isOpen).toBeFalse();
  });

  it('should select option on click', () => {
    spyOn(component.onChange, 'emit');
    component.isOpen = true;
    fixture.detectChanges();

    const items = fixture.debugElement.queryAll(By.css('li'));
    items[1].nativeElement.click();
    fixture.detectChanges();

    expect(component.value).toBe(2);
    expect(component.isOpen).toBeFalse();
    expect(component.onChange.emit).toHaveBeenCalledWith({ value: 2, originalEvent: jasmine.any(Event) });
  });

  it('should navigate options with arrow keys and select with Enter', () => {
    const trigger = fixture.debugElement.query(By.css('.focus\\:border-primary'));

    // Down Arrow to open and focus first option
    const downEvent = new KeyboardEvent('keydown', { key: 'ArrowDown', bubbles: true });
    trigger.nativeElement.dispatchEvent(downEvent);
    fixture.detectChanges();
    expect(component.isOpen).toBeTrue();
    expect(component.focusedIndex).toBe(0);

    // Down Arrow again to focus second option
    trigger.nativeElement.dispatchEvent(downEvent);
    fixture.detectChanges();
    expect(component.focusedIndex).toBe(1);

    // Enter to select focused option
    const enterEvent = new KeyboardEvent('keydown', { key: 'Enter', bubbles: true });
    trigger.nativeElement.dispatchEvent(enterEvent);
    fixture.detectChanges();

    expect(component.value).toBe(2);
    expect(component.isOpen).toBeFalse();
  });

  it('should close on Escape', () => {
    component.isOpen = true;
    fixture.detectChanges();

    const trigger = fixture.debugElement.query(By.css('.focus\\:border-primary'));
    const escEvent = new KeyboardEvent('keydown', { key: 'Escape', bubbles: true });
    trigger.nativeElement.dispatchEvent(escEvent);
    fixture.detectChanges();

    expect(component.isOpen).toBeFalse();
  });

  it('should return option value or index from trackByOption', () => {
    expect(component.trackByOption(0, { label: 'Test', value: 42 })).toBe(42);
    expect(component.trackByOption(3, { label: 'Test', value: undefined })).toBe(3);
  });
});
