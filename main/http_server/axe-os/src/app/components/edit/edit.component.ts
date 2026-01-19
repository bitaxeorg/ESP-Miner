import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit, OnDestroy, OnChanges, SimpleChanges } from '@angular/core';
import { FormBuilder, FormGroup, FormControl, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { forkJoin, startWith, Subject, takeUntil, pairwise, BehaviorSubject, Observable } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemApiService } from 'src/app/services/system.service';
import { ActivatedRoute } from '@angular/router';

type Dropdown = {
  name: string;
  value: number;
}[]

const DISPLAY_TIMEOUT_STEPS = [0, 1, 2, 5, 15, 30, 60, 60 * 2, 60 * 4, 60* 8, -1];
const STATS_FREQUENCY_STEPS = [0, 30, 60, 60 * 2, 60 * 6, 60 * 14, 60 * 28, 60 * 60];

@Component({
  selector: 'app-edit',
  templateUrl: './edit.component.html'
})

export class EditComponent implements OnInit, OnDestroy, OnChanges {
  private formSubject = new BehaviorSubject<FormGroup | null>(null);
  public form$: Observable<FormGroup | null> = this.formSubject.asObservable();

  public form!: FormGroup;

  public firmwareUpdateProgress: number | null = null;
  public websiteUpdateProgress: number | null = null;

  public savedChanges: boolean = false;
  public settingsUnlocked: boolean = false;

  @Input() uri = '';

  // Store frequency and voltage options from API
  public defaultFrequency: number = 0;
  public frequencyOptions: number[] = [];
  public defaultVoltage: number = 0;
  public voltageOptions: number[] = [];

  private destroy$ = new Subject<void>();

  public displays = ["NONE", "SSD1306 (128x32)", "SSD1309 (128x64)", "SH1107 (64x128)", "SH1107 (128x128)"];
  public rotations = [0, 90, 180, 270];
  public displayTimeoutControl: FormControl;
  public statsFrequencyControl: FormControl;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemApiService,
    private toastr: ToastrService,
    private loadingService: LoadingService,
    private route: ActivatedRoute,
  ) {
    // Check URL parameter for settings unlock
    this.route.queryParams.subscribe(params => {
      const urlOcParam = params['oc'] !== undefined;
      if (urlOcParam) {
        // If ?oc is in URL, enable overclock and save to NVS
        this.settingsUnlocked = true;
        this.saveOverclockSetting(1);
        console.log(
          '🎉 The ancient seals have been broken!\n' +
          '⚡ Unlimited power flows through your miner...\n' +
          '🔧 You can now set custom frequency and voltage values.\n' +
          '⚠️ Remember: with great power comes great responsibility!'
        );
      } else {
        // If ?oc is not in URL, check NVS setting (will be loaded in ngOnInit)
        console.log('🔒 Here be dragons! Advanced settings are locked for your protection. \n' +
          'Only the bravest miners dare to venture forth... \n' +
          'If you wish to unlock dangerous overclocking powers, add: %c?oc',
          'color: #ff4400; text-decoration: underline; cursor: pointer; font-weight: bold;',
          'to the current URL'
        );
      }
    });

    this.displayTimeoutControl = new FormControl();
    this.displayTimeoutControl.valueChanges.pipe(pairwise()).subscribe(([prev, next]) => {
      if (prev === next) {
        return;
      }

      this.form.patchValue({ displayTimeout: DISPLAY_TIMEOUT_STEPS[next] });
      this.form.controls['displayTimeout'].markAsDirty();
    });

    this.statsFrequencyControl = new FormControl();
    this.statsFrequencyControl.valueChanges.pipe(pairwise()).subscribe(([prev, next]) => {
      if (prev === next) {
        return;
      }

      this.form.patchValue({ statsFrequency: STATS_FREQUENCY_STEPS[next] });
      this.form.controls['statsFrequency'].markAsDirty();
    });
  }

  private saveOverclockSetting(enabled: number) {
    const deviceUri = this.uri || '';
    this.systemService.updateSystem(deviceUri, { overclockEnabled: enabled })
      .subscribe({
        next: () => {
          console.log(`Overclock setting saved: ${enabled === 1 ? 'enabled' : 'disabled'}`);
        },
        error: (err) => {
          console.error(`Failed to save overclock setting: ${err.message}`);
        }
      });
  }

  ngOnInit(): void {
    this.loadDeviceSettings();
  }

  ngOnChanges(changes: SimpleChanges): void {
    // When URI changes, reload the device settings
    if (changes['uri'] && changes['uri'].currentValue && !changes['uri'].firstChange) {
      this.loadDeviceSettings();
    }
  }

  private loadDeviceSettings(): void {
    const deviceUri = this.uri || '';


    // Fetch both system info and ASIC settings in parallel
    forkJoin({
      info: this.systemService.getInfo(deviceUri),
      asic: this.systemService.getAsicSettings(deviceUri)
    })
    .pipe(
      this.loadingService.lockUIUntilComplete(),
      takeUntil(this.destroy$)
    )
    .subscribe(({ info, asic }) => {
      // Store the frequency and voltage options from the API
      this.defaultFrequency = asic.defaultFrequency;
      this.frequencyOptions = asic.frequencyOptions;
      this.defaultVoltage = asic.defaultVoltage;
      this.voltageOptions = asic.voltageOptions;

      // Check if overclock is enabled in NVS
      if (info.overclockEnabled === 1) {
        this.settingsUnlocked = true;
        console.log(
          '🎉 Overclock mode is enabled from NVS settings!\n' +
          '⚡ Custom frequency and voltage values are available.'
        );
      }

        this.form = this.fb.group({
          display: [info.display, [Validators.required]],
          rotation: [info.rotation, [Validators.required]],
          invertscreen: [info.invertscreen == 1],
          displayTimeout: [info.displayTimeout, [
            Validators.required,
            Validators.min(-1),
            Validators.max(this.displayTimeoutMaxValue)
          ]],
          coreVoltage: [info.coreVoltage, [Validators.required]],
          frequency: [info.frequency, [Validators.required]],
          autofanspeed: [info.autofanspeed == 1, [Validators.required]],
          minfanspeed: [info.minFanSpeed, [Validators.required]],
          manualFanSpeed: [info.manualFanSpeed, [Validators.required]],
          temptarget: [info.temptarget, [Validators.required]],
          overheat_mode: [info.overheat_mode, [Validators.required]],
          statsFrequency: [info.statsFrequency, [
            Validators.required,
            Validators.min(0),
            Validators.max(this.statsFrequencyMaxValue)
          ]]
        });

        this.formSubject.next(this.form);

      this.form.controls['autofanspeed'].valueChanges.pipe(
        startWith(this.form.controls['autofanspeed'].value),
        takeUntil(this.destroy$)
      ).subscribe(autofanspeed => {
        if (autofanspeed) {
          this.form.controls['manualFanSpeed'].disable();
          this.form.controls['temptarget'].enable();
        } else {
          this.form.controls['manualFanSpeed'].enable();
          this.form.controls['temptarget'].disable();
        }
      });

      // Add custom value to predefined steps
      if (DISPLAY_TIMEOUT_STEPS.filter(x => x === info.displayTimeout).length === 0) {
        DISPLAY_TIMEOUT_STEPS.push(info.displayTimeout);
        DISPLAY_TIMEOUT_STEPS.sort((a, b) => a - b);
        DISPLAY_TIMEOUT_STEPS.push(DISPLAY_TIMEOUT_STEPS.shift() as number);
      }

      this.displayTimeoutControl.setValue(
        DISPLAY_TIMEOUT_STEPS.findIndex(x => x === info.displayTimeout)
      );

      // Add custom value to predefined steps
      if (STATS_FREQUENCY_STEPS.filter(x => x === info.statsFrequency).length === 0) {
        STATS_FREQUENCY_STEPS.push(info.statsFrequency);
        STATS_FREQUENCY_STEPS.sort((a, b) => a - b);
      }

      this.statsFrequencyControl.setValue(
        STATS_FREQUENCY_STEPS.findIndex(x => x === info.statsFrequency)
      );
    });
  }

  ngOnDestroy(): void {
    this.destroy$.next();
    this.destroy$.complete();
  }

  public updateSystem() {
    const form = this.form.getRawValue();

    if (form.stratumPassword === '*****') {
      delete form.stratumPassword;
    }

    const deviceUri = this.uri || '';
    this.systemService.updateSystem(deviceUri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Saved settings for ${this.uri}` : 'Saved settings';
          if (this.isRestartRequired) {
            this.toastr.warning('You must restart this device after saving for changes to take effect.');
          }
          this.toastr.success(successMessage);
          this.savedChanges = true;
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Could not save settings for ${this.uri}. ${err.message}` : `Could not save settings. ${err.message}`;
          this.toastr.error(errorMessage);
          this.savedChanges = false;
        }
      });
  }

  disableOverheatMode() {
    this.form.patchValue({ overheat_mode: 0 });
    this.updateSystem();
  }

  toggleOverclockMode(enable: boolean) {
    this.settingsUnlocked = enable;
    this.saveOverclockSetting(enable ? 1 : 0);

    if (enable) {
      console.log(
        '🎉 Overclock mode enabled!\n' +
        '⚡ Custom frequency and voltage values are now available.'
      );
    } else {
      console.log('🔒 Overclock mode disabled. Using safe preset values only.');
    }
  }

  public restart() {
    this.systemService.restart(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Device at ${this.uri} restarted` : 'Device restarted';
          this.toastr.success(successMessage);
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Failed to restart device at ${this.uri}. ${err.message}` : `Failed to restart device. ${err.message}`;
          this.toastr.error(errorMessage);
        }
      });
  }

  get dropdownFrequency(): Dropdown {
    return this.buildDropdown('frequency', this.frequencyOptions, this.defaultFrequency);
  }

  get dropdownVoltage(): Dropdown {
    return this.buildDropdown('coreVoltage', this.voltageOptions, this.defaultVoltage);
  }

  get displayTimeoutMaxSteps(): number {
    return DISPLAY_TIMEOUT_STEPS.length - 1;
  }

  get displayTimeoutMaxValue(): number {
    return DISPLAY_TIMEOUT_STEPS[this.displayTimeoutMaxSteps - 1];
  }

  get statsFrequencyMaxSteps(): number {
    return STATS_FREQUENCY_STEPS.length - 1;
  }

  get statsFrequencyMaxValue(): number {
    return STATS_FREQUENCY_STEPS[this.statsFrequencyMaxSteps];
  }

  buildDropdown(formField: string, apiOptions: number[], defaultValue: number): Dropdown {
    if (!apiOptions.length) {
      return [];
    }

    // Convert options from API to dropdown format
    const options = apiOptions.map(option => {
      return {
        name: defaultValue === option ? `${option} (Default)` : `${option}`,
        value: option
      };
    });

    // Get current field value from form
    const currentValue = this.form?.get(formField)?.value;

    // If current field value exists and isn't in the options
    if (currentValue && !options.some(opt => opt.value === currentValue)) {
      options.push({
        name: `${currentValue} (Custom)`,
        value: currentValue
      });
      // Sort options by value
      options.sort((a, b) => a.value - b.value);
    }

    return options;
  }

  get noRestartFields(): string[] {
    return [
      'displayTimeout',
      'coreVoltage',
      'frequency',
      'autofanspeed',
      'manualFanSpeed',
      'temptarget',
      'overheat_mode',
      'statsFrequency'
    ];
  }

  get isRestartRequired(): boolean {
    return !! Object.entries(this.form.controls)
      .filter(([field, control]) => control.dirty && !this.noRestartFields.includes(field)).length
  }
}
