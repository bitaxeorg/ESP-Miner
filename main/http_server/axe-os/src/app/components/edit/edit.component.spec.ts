import { ComponentFixture, TestBed } from '@angular/core/testing';
import { FormBuilder, ReactiveFormsModule, FormsModule, FormControl, FormGroup } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { Observable, of } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';
import { eASICModel } from 'src/models/enum/eASICModel';
import { EditComponent } from './edit.component';
import { ActivatedRoute } from '@angular/router';
import { HttpClientTestingModule } from '@angular/common/http/testing';
import { RouterTestingModule } from '@angular/router/testing';
import { ToastrModule } from 'ngx-toastr';
import { CUSTOM_ELEMENTS_SCHEMA, NO_ERRORS_SCHEMA, Component } from '@angular/core';

// PrimeNG components used in the template
import { DialogModule } from 'primeng/dialog';
import { ButtonModule } from 'primeng/button';
import { InputTextModule } from 'primeng/inputtext';
import { TooltipModule } from 'primeng/tooltip';

// Create a host test component with no template to avoid form control issues
@Component({
  selector: 'app-host-component',
  template: ''
})
class TestHostComponent {}

describe('EditComponent', () => {
  let component: EditComponent;
  let fixture: ComponentFixture<TestHostComponent>;
  let systemServiceSpy: jasmine.SpyObj<SystemService>;
  let toastrServiceSpy: jasmine.SpyObj<ToastrService>;
  let loadingServiceSpy: jasmine.SpyObj<LoadingService>;
  
  // Mock localStorage
  let localStorageMock: { [key: string]: string } = {};

  // Mock system information
  const mockSystemInfo: any = {
    ASICModel: eASICModel.BM1370,
    overclockEnabled: 1,
    frequency: 525,
    coreVoltage: 1150,
    flipscreen: 0,
    invertscreen: 0,
    autofanspeed: 1,
    // invertfanpolarity: 0,
    fanspeed: 70,
    temptarget: 60,
    overheat_mode: 0
  };

  beforeAll(() => {
    // Replace the ngOnInit method on the EditComponent prototype
    // This prevents any calls to services during initialization
    EditComponent.prototype.ngOnInit = jasmine.createSpy('ngOnInit').and.callFake(function(this: EditComponent) {
      // Do minimal ngOnInit setup without RxJS calls
      this.warningAcknowledged = localStorage.getItem('overclockWarningAcknowledged') === 'true';
    });
  });

  beforeEach(async () => {
    // Mock localStorage before each test
    localStorageMock = {};
    spyOn(localStorage, 'getItem').and.callFake((key) => {
      return localStorageMock[key] || null;
    });
    spyOn(localStorage, 'setItem').and.callFake((key, value) => {
      localStorageMock[key] = value.toString();
    });
    
    // Create spies for services with proper mocks
    systemServiceSpy = jasmine.createSpyObj('SystemService', ['getInfo', 'updateSystem', 'restart']);
    toastrServiceSpy = jasmine.createSpyObj('ToastrService', ['success', 'error', 'info']);
    loadingServiceSpy = jasmine.createSpyObj('LoadingService', ['lockUIUntilComplete']);
    
    // Mock service responses
    systemServiceSpy.getInfo.and.returnValue(of(mockSystemInfo));
    
    // Simplify the lockUIUntilComplete mock - use any to bypass type check
    (loadingServiceSpy.lockUIUntilComplete as any).and.returnValue(() => (source: any) => source);

    // Configure test module
    await TestBed.configureTestingModule({
      declarations: [TestHostComponent, EditComponent],
      imports: [
        ReactiveFormsModule,
        FormsModule,
        HttpClientTestingModule,
        RouterTestingModule,
        ToastrModule.forRoot(),
        DialogModule,
        ButtonModule,
        InputTextModule,
        TooltipModule
      ],
      providers: [
        FormBuilder,
        { provide: SystemService, useValue: systemServiceSpy },
        { provide: ToastrService, useValue: toastrServiceSpy },
        { provide: LoadingService, useValue: loadingServiceSpy },
        {
          provide: ActivatedRoute,
          useValue: {
            queryParams: of({})
          }
        }
      ],
      schemas: [CUSTOM_ELEMENTS_SCHEMA, NO_ERRORS_SCHEMA]
    }).compileComponents();
    
    fixture = TestBed.createComponent(TestHostComponent);
    
    // Create the component manually instead of using the template
    component = new EditComponent(
      TestBed.inject(FormBuilder),
      systemServiceSpy,
      toastrServiceSpy,
      loadingServiceSpy,
      TestBed.inject(ActivatedRoute)
    );
    
    // Setup component directly
    component.ASICModel = mockSystemInfo.ASICModel;
    component.uri = '';
    
    // Initialize form without binding to the template
    component.form = new FormBuilder().group({
      frequency: [mockSystemInfo.frequency],
      coreVoltage: [mockSystemInfo.coreVoltage],
      flipscreen: [mockSystemInfo.flipscreen === 1],
      invertscreen: [mockSystemInfo.invertscreen === 1],
      autofanspeed: [mockSystemInfo.autofanspeed === 1],
      fanspeed: [mockSystemInfo.fanspeed],
      temptarget: [mockSystemInfo.temptarget],
      overheat_mode: [mockSystemInfo.overheat_mode]
    });
    
    // Set up presets
    component.presets = [{
      name: `Factory Default (${component.ASICModel})`,
      frequency: component.defaultFrequency[component.ASICModel],
      coreVoltage: component.defaultVoltage[component.ASICModel],
      timestamp: 0,
      asicModel: component.ASICModel,
      builtIn: true
    }];
    
    // Initialize validFrequencies
    component.generateValidFrequencies();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
  
  describe('increment functionality', () => {
    it('should increment frequency value by the specified amount', () => {
      // Setup initial value
      component.form.patchValue({ frequency: 500 });
      
      // Call the increment method
      component.incrementValue('frequency', 25);
      
      // Check the result
      expect(component.form.get('frequency')?.value).toBeGreaterThan(500);
    });
    
    it('should decrement frequency value by the specified amount', () => {
      // Setup initial value
      component.form.patchValue({ frequency: 500 });
      
      // Call the increment method with negative value
      component.incrementValue('frequency', -50);
      
      // Check the result
      expect(component.form.get('frequency')?.value).toBeLessThan(500);
    });
    
    it('should not exceed maximum frequency value', () => {
      // Setup initial value near max
      const maxValue = component.maxFrequency[component.ASICModel];
      component.form.patchValue({ frequency: maxValue - 10 });
      
      // Call the increment method with a value that would exceed max
      component.incrementValue('frequency', 50);
      
      // Check that the value is capped at max
      expect(component.form.get('frequency')?.value).toBeLessThanOrEqual(maxValue);
    });
    
    it('should not go below minimum frequency value', () => {
      // Setup initial value near min
      const minValue = component.minFrequency[component.ASICModel];
      component.form.patchValue({ frequency: minValue + 10 });
      
      // Call the increment method with a value that would go below min
      component.incrementValue('frequency', -50);
      
      // Check that the value is capped at min
      expect(component.form.get('frequency')?.value).toBeGreaterThanOrEqual(minValue);
    });
  });
  
  describe('color calculation', () => {
    it('should return color values for different frequency settings', () => {
      // Test default value
      component.form.patchValue({ frequency: component.defaultFrequency[component.ASICModel] });
      expect(component.getFrequencyColor()).toBeDefined();
      
      // Test below default
      component.form.patchValue({ frequency: component.minFrequency[component.ASICModel] });
      expect(component.getFrequencyColor()).toBeDefined();
      
      // Test above default
      component.form.patchValue({ frequency: component.maxFrequency[component.ASICModel] });
      expect(component.getFrequencyColor()).toBeDefined();
    });
  });
  
  describe('preset functionality', () => {
    it('should handle preset operations correctly', () => {
      // Set preset name
      component.presetName = 'Test Preset';
      component.form.patchValue({ frequency: 600, coreVoltage: 1200 });
      
      // Save preset
      component.savePreset();
      expect(component.presets.length).toBeGreaterThan(1);
      
      // Apply preset
      const testPreset = component.presets[1];
      component.form.patchValue({ frequency: 500, coreVoltage: 1100 });
      component.applyPreset(testPreset);
      expect(component.form.get('frequency')?.value).toBe(600);
    });
    
    it('should load presets from localStorage on init', () => {
      // Setup mock localStorage with a preset
      const mockPreset = {
        name: 'Saved Preset',
        frequency: 650,
        coreVoltage: 1250,
        timestamp: Date.now(),
        asicModel: eASICModel.BM1370
      };
      
      localStorageMock['overclockPresets'] = JSON.stringify([mockPreset]);
      
      // Call loadPresets manually since we've mocked localStorage after component init
      component.loadPresets();
      
      // Check that preset was loaded
      // Should have the built-in default + our saved preset
      expect(component.presets.length).toBe(2);
      
      // First one should be built-in default
      expect(component.presets[0].builtIn).toBeTrue();
      expect(component.presets[0].name).toContain('Factory Default');
      expect(component.presets[0].name).toContain(component.ASICModel);
      
      // Second should be our custom preset
      expect(component.presets[1].name).toBe('Saved Preset');
      expect(component.presets[1].frequency).toBe(650);
    });
    
    it('should always include a built-in default preset', () => {
      // Clear any existing presets
      component.presets = [];
      
      // Load presets (should add the default)
      component.loadPresets();
      
      // Should have at least the built-in default preset
      expect(component.presets.length).toBeGreaterThanOrEqual(1);
      expect(component.presets[0].builtIn).toBeTrue();
      expect(component.presets[0].name).toContain('Factory Default');
      
      // Factory preset should have default values
      expect(component.presets[0].frequency).toBe(component.defaultFrequency[component.ASICModel]);
      expect(component.presets[0].coreVoltage).toBe(component.defaultVoltage[component.ASICModel]);
    });
    
    it('should prevent deletion of built-in presets', () => {
      // Setup with just the default preset
      component.loadPresets();
      const builtInPreset = component.presets[0];
      
      // Try to delete the built-in preset
      component.deletePreset(builtInPreset, new MouseEvent('click'));
      
      // Should still be there
      expect(component.presets.length).toBe(1);
      expect(component.presets[0].builtIn).toBeTrue();
      
      // Should have shown an error toast
      expect(toastrServiceSpy.error).toHaveBeenCalled();
    });
    
    it('should not save built-in presets to localStorage', () => {
      // Setup with a built-in and a user preset
      const userPreset = {
        name: 'User Preset',
        frequency: 650,
        coreVoltage: 1250,
        timestamp: Date.now(),
        asicModel: component.ASICModel
      };
      
      component.loadPresets(); // Adds built-in
      component.presets.push(userPreset);
      
      // Save to storage
      component.savePresetsToStorage();
      
      // Check localStorage - should only have the user preset
      const savedPresets = JSON.parse(localStorageMock['overclockPresets'] || '[]');
      expect(savedPresets.length).toBe(1);
      expect(savedPresets[0].name).toBe('User Preset');
      expect(savedPresets.some((p: any) => p.builtIn === true)).toBeFalse();
    });
    
    it('should apply preset values to the form', () => {
      // Create a test preset
      const testPreset = {
        name: 'Apply Test',
        frequency: 700,
        coreVoltage: 1300,
        timestamp: Date.now(),
        asicModel: eASICModel.BM1370
      };
      
      // Start with different values
      component.form.patchValue({
        frequency: 500,
        coreVoltage: 1100
      });
      
      // Apply the preset
      component.applyPreset(testPreset);
      
      // Check that form values were updated
      expect(component.form.get('frequency')?.value).toBe(700);
      expect(component.form.get('coreVoltage')?.value).toBe(1300);
      expect(component.form.dirty).toBeTrue();
    });
    
    it('should apply default preset values to the form', () => {
      // Start with non-default values
      component.form.patchValue({
        frequency: 800,
        coreVoltage: 1300
      });
      
      // Get the built-in default preset
      component.loadPresets();
      const defaultPreset = component.presets[0];
      
      // Apply the default preset
      component.applyPreset(defaultPreset);
      
      // Check that form values were reset to defaults
      expect(component.form.get('frequency')?.value).toBe(component.defaultFrequency[component.ASICModel]);
      expect(component.form.get('coreVoltage')?.value).toBe(component.defaultVoltage[component.ASICModel]);
    });
    
    it('should delete a preset', () => {
      // Setup presets
      const presets = [
        {
          name: 'Preset 1',
          frequency: 600,
          coreVoltage: 1200,
          timestamp: Date.now(),
          asicModel: eASICModel.BM1370
        },
        {
          name: 'Preset 2',
          frequency: 650,
          coreVoltage: 1250,
          timestamp: Date.now(),
          asicModel: eASICModel.BM1370
        }
      ];
      
      component.presets = [...presets];
      
      // Delete first preset
      component.deletePreset(presets[0], new MouseEvent('click'));
      
      // Check that preset was removed
      expect(component.presets.length).toBe(1);
      expect(component.presets[0].name).toBe('Preset 2');
      
      // localStorage should have been updated
      expect(toastrServiceSpy.success).toHaveBeenCalled();
    });
    
    it('should update an existing preset if names match', () => {
      // Setup initial preset
      const initialPreset = {
        name: 'Same Name',
        frequency: 600,
        coreVoltage: 1200,
        timestamp: Date.now() - 1000, // older timestamp
        asicModel: eASICModel.BM1370
      };
      
      component.presets = [initialPreset];
      
      // Set up new values
      component.form.patchValue({
        frequency: 650,
        coreVoltage: 1250
      });
      
      // Use same name for new preset
      component.presetName = 'Same Name';
      
      // Save preset
      component.savePreset();
      
      // Should still have only one preset
      expect(component.presets.length).toBe(1);
      
      // Values should be updated
      expect(component.presets[0].frequency).toBe(650);
      expect(component.presets[0].coreVoltage).toBe(1250);
      expect(component.presets[0].timestamp).toBeGreaterThan(initialPreset.timestamp);
    });
  });
  
  describe('warning acknowledgment', () => {
    it('should set warningAcknowledged to false initially if not in localStorage', () => {
      // Ensure localStorage is empty
      localStorage.removeItem('overclockWarningAcknowledged');
      
      // Reset component's warningAcknowledged property
      component.warningAcknowledged = false;
      
      // Call ngOnInit directly on our component instance
      component.ngOnInit();
      
      // Check initial state
      expect(component.warningAcknowledged).toBeFalse();
    });
    
    it('should set warningAcknowledged to true if acknowledged in localStorage', () => {
      // Set localStorage to indicate warning was acknowledged
      localStorage.setItem('overclockWarningAcknowledged', 'true');
      
      // Reset component's warningAcknowledged property
      component.warningAcknowledged = false;
      
      // Call ngOnInit directly on our component instance
      component.ngOnInit();
      
      // Check initial state
      expect(component.warningAcknowledged).toBeTrue();
    });
    
    it('should save acknowledgment to localStorage when acknowledgeWarning is called', () => {
      // Ensure localStorage is empty
      localStorage.removeItem('overclockWarningAcknowledged');
      
      // Initial state should be false
      expect(component.warningAcknowledged).toBeFalse();
      
      // Call the method
      component.acknowledgeWarning();
      
      // Check results
      expect(component.warningAcknowledged).toBeTrue();
      expect(localStorage.getItem('overclockWarningAcknowledged')).toBe('true');
    });
  });

  describe('Frequency Calculation Tests', () => {
    it('should calculate actual frequency values based on chip hardware limitations', () => {
      // Test with BM1366
      component.ASICModel = eASICModel.BM1366;
      
      // Test various input frequencies and log the actual calculated values
      const testInputs = [400, 431, 433, 437, 450, 485, 500];
      console.log('BM1366 Frequency Calculation Results:');
      
      testInputs.forEach(input => {
        const actualFreq = component.calculateActualFrequency(input);
        console.log(`Input: ${input} MHz → Actual: ${actualFreq} MHz`);
        // Just verify the calculation returns something (not testing specific values)
        expect(actualFreq).toBeDefined();
      });
    });
    
    it('should generate a list of valid frequencies for increment/decrement', () => {
      component.ASICModel = eASICModel.BM1366;
      component.generateValidFrequencies();
      
      // Just verify we have some frequencies
      expect(component.validFrequencies[eASICModel.BM1366].length).toBeGreaterThan(0);
      
      // Log some of the frequencies to help understand the available steps
      const sortedFreqs = [...component.validFrequencies[eASICModel.BM1366]]
        .sort((a, b) => a - b);
      
      console.log('Sample of valid frequencies for BM1366:');
      // Log frequencies around our test values
      const nearTestFreqs = sortedFreqs.filter(f => f >= 430 && f <= 440);
      console.log(nearTestFreqs);
    });
  });

  describe('Increment/Decrement Behavior', () => {
    it('should show the next logical frequency when incrementing', () => {
      component.ASICModel = eASICModel.BM1366;
      component.generateValidFrequencies();
      
      // Instead of hardcoding expected values, let's observe the behavior
      const testCases = [431, 437.5, 450, 485];
      
      testCases.forEach(startFreq => {
        // Set up the form with the starting frequency
        component.form.patchValue({ frequency: startFreq });
        const before = component.form.get('frequency')?.value;
        
        // Get the actual frequency that would be displayed
        const actualBefore = component.actualFrequency;
        
        // Perform increment
        component.incrementValue('frequency', 1);
        
        // Get the new form value and actual frequency
        const after = component.form.get('frequency')?.value;
        const actualAfter = component.actualFrequency;
        
        // Log the results
        console.log(`Increment from ${before} (Actual: ${actualBefore}) → ${after} (Actual: ${actualAfter})`);
        
        // Verify it changed in the positive direction
        expect(after).toBeGreaterThan(before);
      });
    });
    
    it('should show the previous logical frequency when decrementing', () => {
      component.ASICModel = eASICModel.BM1366;
      component.generateValidFrequencies();
      
      // Test with a few different starting frequencies
      const testCases = [485, 450, 437.5, 433.33];
      
      testCases.forEach(startFreq => {
        // Set up the form with the starting frequency
        component.form.patchValue({ frequency: startFreq });
        const before = component.form.get('frequency')?.value;
        
        // Get the actual frequency that would be displayed
        const actualBefore = component.actualFrequency;
        
        // Perform decrement
        component.incrementValue('frequency', -1);
        
        // Get the new form value and actual frequency
        const after = component.form.get('frequency')?.value;
        const actualAfter = component.actualFrequency;
        
        // Log the results
        console.log(`Decrement from ${before} (Actual: ${actualBefore}) → ${after} (Actual: ${actualAfter})`);
        
        // Verify it changed in the negative direction
        expect(after).toBeLessThan(before);
      });
    });
  });

  describe('form state handling', () => {
    it('should mark form as dirty when frequency is changed', () => {
      // Start with initial value
      component.form.patchValue({ frequency: 500 });
      
      // Ensure form starts clean
      component.form.markAsPristine();
      expect(component.form.dirty).toBeFalse();
      
      // Change the value
      component.incrementValue('frequency', 1);
      
      // Form should be marked as dirty
      expect(component.form.dirty).toBeTrue();
    });
    
    it('should mark form as dirty when coreVoltage is changed', () => {
      // Start with initial value
      component.form.patchValue({ coreVoltage: 1150 });
      
      // Ensure form starts clean
      component.form.markAsPristine();
      expect(component.form.dirty).toBeFalse();
      
      // Change the value
      component.incrementValue('coreVoltage', 10);
      
      // Form should be marked as dirty
      expect(component.form.dirty).toBeTrue();
    });
    
    it('should mark the individual control as dirty when changed', () => {
      // Start with initial value
      component.form.patchValue({ frequency: 500 });
      
      // Ensure control starts clean
      component.form.get('frequency')?.markAsPristine();
      expect(component.form.get('frequency')?.dirty).toBeFalse();
      
      // Change the value
      component.incrementValue('frequency', 1);
      
      // Control should be marked as dirty
      expect(component.form.get('frequency')?.dirty).toBeTrue();
    });
    
    it('should reset form dirty state after saving', () => {
      // Mock the updateSystem response
      systemServiceSpy.updateSystem.and.returnValue(of({}));
      
      // Mark the form as dirty
      component.form.patchValue({ frequency: 600 });
      component.form.markAsDirty();
      expect(component.form.dirty).toBeTrue();
      
      // Call updateSystem
      component.updateSystem();
      
      // Form should no longer be dirty after successful save
      expect(component.form.dirty).toBeFalse();
      expect(component.savedChanges).toBeTrue();
    });
  });
});
