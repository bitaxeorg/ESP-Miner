import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit, OnDestroy } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { startWith, Subject, takeUntil } from 'rxjs';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';
import { eASICModel } from 'src/models/enum/eASICModel';
import { ActivatedRoute } from '@angular/router';

interface OverclockPreset {
  name: string;
  frequency: number;
  coreVoltage: number;
  timestamp: number;
  asicModel: eASICModel;
  builtIn?: boolean;
}

@Component({
  selector: 'app-edit',
  templateUrl: './edit.component.html',
  styleUrls: ['./edit.component.scss']
})

export class EditComponent implements OnInit, OnDestroy {



  public form!: FormGroup;

  public firmwareUpdateProgress: number | null = null;
  public websiteUpdateProgress: number | null = null;

  public savedChanges: boolean = false;
  public settingsUnlocked: boolean = false;
  public eASICModel = eASICModel;
  public ASICModel!: eASICModel;
  public restrictedModels: eASICModel[] = Object.values(eASICModel)
    .filter((v): v is eASICModel => typeof v === 'string');

  public defaultFrequency: { [key in eASICModel]: number } = {
    [eASICModel.BM1366]: 485,
    [eASICModel.BM1368]: 490,
    [eASICModel.BM1370]: 525,
    [eASICModel.BM1397]: 425
  };

  public defaultVoltage: { [key in eASICModel]: number } = {
    [eASICModel.BM1366]: 1200,
    [eASICModel.BM1368]: 1166,
    [eASICModel.BM1370]: 1150,
    [eASICModel.BM1397]: 1300
  };

  public maxFrequency: { [key in eASICModel]: number } = {
    [eASICModel.BM1366]: 700,
    [eASICModel.BM1368]: 700,
    [eASICModel.BM1370]: 950,
    [eASICModel.BM1397]: 675
  };

  public maxVoltage: { [key in eASICModel]: number } = {
    [eASICModel.BM1366]: 1350,
    [eASICModel.BM1368]: 1350,
    [eASICModel.BM1370]: 1350,
    [eASICModel.BM1397]: 1550
  };

  public minFrequency: { [key in eASICModel]: number } = {
    [eASICModel.BM1366]: 50,
    [eASICModel.BM1368]: 50,
    [eASICModel.BM1370]: 50,
    [eASICModel.BM1397]: 50
  };

  public minVoltage: { [key in eASICModel]: number } = {
    [eASICModel.BM1366]: 900,
    [eASICModel.BM1368]: 900,
    [eASICModel.BM1370]: 900,
    [eASICModel.BM1397]: 900
  };

  public incrementOptions = [1, 10, 25, 50, 100];
  
  public get reversedIncrementOptions(): number[] {
    return [...this.incrementOptions].reverse();
  }

  public validFrequencies: { [key in eASICModel]: number[] } = {
    [eASICModel.BM1366]: [],
    [eASICModel.BM1368]: [],
    [eASICModel.BM1370]: [],
    [eASICModel.BM1397]: []
  };

  public get actualFrequency(): number {
    if (!this.form?.get('frequency')?.value) {
      return this.defaultFrequency[this.ASICModel];
    }
    const actual = this.calculateActualFrequency(this.form.get('frequency')?.value);
    if (!actual) {
      return 0;
    }
    return actual;
  }

  public presetName: string = '';
  public presets: OverclockPreset[] = [];
  public showPresetDialog: boolean = false;
  public editingPreset: OverclockPreset | null = null;
  
  @Input() uri = '';

  public BM1397DropdownFrequency = [
    { name: '400', value: 400 },
    { name: '425 (default)', value: 425 },
    { name: '450', value: 450 },
    { name: '475', value: 475 },
    { name: '485', value: 485 },
    { name: '500', value: 500 },
    { name: '525', value: 525 },
    { name: '550', value: 550 },
    { name: '575', value: 575 },
    { name: '590', value: 590 },
    { name: '600', value: 600 },
    { name: '610', value: 610 },
    { name: '620', value: 620 },
    { name: '630', value: 630 },
    { name: '640', value: 640 },
    { name: '650', value: 650 },
  ];

  public BM1366DropdownFrequency = [
    { name: '400', value: 400 },
    { name: '425', value: 425 },
    { name: '450', value: 450 },
    { name: '475', value: 475 },
    { name: '485 (default)', value: 485 },
    { name: '500', value: 500 },
    { name: '525', value: 525 },
    { name: '550', value: 550 },
    { name: '575', value: 575 },
  ];

  public BM1368DropdownFrequency = [
    { name: '400', value: 400 },
    { name: '425', value: 425 },
    { name: '450', value: 450 },
    { name: '475', value: 475 },
    { name: '490 (default)', value: 490 },
    { name: '500', value: 500 },
    { name: '525', value: 525 },
    { name: '550', value: 550 },
    { name: '575', value: 575 },
  ];

  public BM1370DropdownFrequency = [
    { name: '400', value: 400 },
    { name: '490', value: 490 },
    { name: '525 (default)', value: 525 },
    { name: '550', value: 550 },
    { name: '575', value: 575 },
    { name: '596', value: 596 },
    { name: '600', value: 600 },
    { name: '625', value: 625 },
    { name: '650', value: 650 },
    { name: '675', value: 675 },
    { name: '700', value: 700 },
    { name: '725', value: 725 },
    { name: '750', value: 750 },
    { name: '775', value: 775 },
    { name: '800', value: 800 },
    { name: '825', value: 825 },
    { name: '850', value: 850 },
    { name: '875', value: 875 },
    { name: '900', value: 900 },
  ];

  public BM1370CoreVoltage = [
    { name: '1000', value: 1000 },
    { name: '1060', value: 1060 },
    { name: '1100', value: 1100 },
    { name: '1150 (default)', value: 1150 },
    { name: '1200', value: 1200 },
    { name: '1250', value: 1250 },
    { name: '1255', value: 1255 },
    { name: '1260', value: 1260 },
    { name: '1265', value: 1265 },
    { name: '1270', value: 1270 },
    { name: '1275', value: 1275 },
    { name: '1280', value: 1280 },
    { name: '1285', value: 1285 },
    { name: '1290', value: 1290 },
    { name: '1295', value: 1295 },
    { name: '1300', value: 1300 }
  ];

  public BM1397CoreVoltage = [
    { name: '1100', value: 1100 },
    { name: '1150', value: 1150 },
    { name: '1200', value: 1200 },
    { name: '1250', value: 1250 },
    { name: '1300', value: 1300 },
    { name: '1350', value: 1350 },
    { name: '1400', value: 1400 },
    { name: '1450', value: 1450 },
    { name: '1500', value: 1500 },
  ];
  public BM1366CoreVoltage = [
    { name: '1100', value: 1100 },
    { name: '1150', value: 1150 },
    { name: '1200 (default)', value: 1200 },
    { name: '1250', value: 1250 },
    { name: '1300', value: 1300 },
  ];
  public BM1368CoreVoltage = [
    { name: '1100', value: 1100 },
    { name: '1150', value: 1150 },
    { name: '1166 (default)', value: 1166 },
    { name: '1200', value: 1200 },
    { name: '1250', value: 1250 },
    { name: '1300', value: 1300 },
  ];
  public warningAcknowledged: boolean = false;

  private destroy$ = new Subject<void>();
  private sortedFreqCache = new Map<eASICModel, number[]>();
  private readonly STORAGE_KEY_WARNING = 'overclockWarningAcknowledged';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private loadingService: LoadingService,
    private route: ActivatedRoute,
  ) {
    this.route.queryParams.subscribe(params => {
      const urlOcParam = params['oc'] !== undefined;
      if (urlOcParam) {
        this.settingsUnlocked = true;
        this.saveOverclockSetting(1);
        console.log(
          '🎉 The ancient seals have been broken!\n' +
          '⚡ Unlimited power flows through your miner...\n' +
          '🔧 You can now set custom frequency and voltage values.\n' +
          '⚠️ Remember: with great power comes great responsibility!'
        );
      } else {
        console.log('🔒 Here be dragons! Advanced settings are locked for your protection. \n' +
          'Only the bravest miners dare to venture forth... \n' +
          'If you wish to unlock dangerous overclocking powers, add: %c?oc',
          'color: #ff4400; text-decoration: underline; cursor: pointer; font-weight: bold;',
          'to the current URL'
        );
      }
    });
  }

  private saveOverclockSetting(enabled: number) {
    this.systemService.updateSystem(this.uri, { overclockEnabled: enabled })
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
    this.warningAcknowledged = localStorage.getItem(this.STORAGE_KEY_WARNING) === 'true';

    this.systemService.getInfo(this.uri)
      .pipe(
        this.loadingService.lockUIUntilComplete(),
        takeUntil(this.destroy$)
      )
      .subscribe(info => {
        this.ASICModel = info.ASICModel;

        this.generateValidFrequencies();

        if (info.overclockEnabled === 1) {
          this.settingsUnlocked = true;
          console.log(
            '🎉 Overclock mode is enabled from NVS settings!\n' +
            '⚡ Custom frequency and voltage values are available.'
          );
        }

        this.form = this.fb.group({
          flipscreen: [info.flipscreen == 1],
          invertscreen: [info.invertscreen == 1],
          coreVoltage: [info.coreVoltage, [Validators.required, Validators.min(this.minVoltage[this.ASICModel]), Validators.max(this.maxVoltage[this.ASICModel])]],
          frequency: [this.deriveFloat(info.frequency), [Validators.required, Validators.min(this.minFrequency[this.ASICModel]), Validators.max(this.maxFrequency[this.ASICModel])]],
          autofanspeed: [info.autofanspeed == 1, [Validators.required]],
          fanspeed: [info.fanspeed, [Validators.required]],
          temptarget: [info.temptarget, [Validators.required]],
          overheat_mode: [info.overheat_mode, [Validators.required]]
        });

        this.form.controls['autofanspeed'].valueChanges.pipe(
          startWith(this.form.controls['autofanspeed'].value),
          takeUntil(this.destroy$)
        ).subscribe(autofanspeed => {
          if (autofanspeed) {
            this.form.controls['fanspeed'].disable();
            this.form.controls['temptarget'].enable();
          } else {
            this.form.controls['fanspeed'].enable();
            this.form.controls['temptarget'].disable();
          }
        });
        
        this.loadPresets();
      });
  }

  ngOnDestroy(): void {
    this.destroy$.next();
    this.destroy$.complete();
  }

  /**
   * Generate all valid frequencies for the current ASIC model
   * This pre-calculates all possible frequencies that can be achieved
   */
  generateValidFrequencies(): void {
    const min = this.minFrequency[this.ASICModel];
    const max = this.maxFrequency[this.ASICModel];
    
    const step = 0.01;
    
    const frequencies: number[] = [];
    
    for (let freq = min; freq <= max; freq += step) {
      const actualFreq = this.calculateActualFrequency(freq);
      
      const isValid = this.ASICModel === eASICModel.BM1370 
        ? actualFreq !== null && Math.abs(actualFreq - freq) <= 0.5
        : actualFreq !== null;
      
      const roundedFreq = parseFloat(actualFreq?.toFixed(2) ?? '0');
      
      if (isValid && !frequencies.some(f => Math.abs(f - roundedFreq) < 0.01)) {
        frequencies.push(roundedFreq);
      }
    }
    
    frequencies.sort((a, b) => a - b);
    this.validFrequencies[this.ASICModel] = frequencies;
    
    console.log(`Generated ${frequencies.length} valid frequencies for ${this.ASICModel}`);
    if (frequencies.length > 0) {
      console.log(`Range: ${frequencies[0]} - ${frequencies[frequencies.length - 1]} MHz`);
    }
  }

  /**
   * Calculate the actual frequency that will be set for a given target frequency
   * Port of the C function to TypeScript
   */
  calculateActualFrequency(targetFreq: number): number | null {
    switch (this.ASICModel) {
      case eASICModel.BM1370:
        return this.calculateBM1370Frequency(targetFreq);
      case eASICModel.BM1366:
        return this.calculateBM1366Frequency(targetFreq);
      case eASICModel.BM1368:
        return this.calculateBM1368Frequency(targetFreq);
      case eASICModel.BM1397:
        return this.calculateBM1397Frequency(targetFreq);
      default:
        return targetFreq;
    }
  }

  /**
   * Calculate the actual frequency for BM1370 chip
   * Direct port of the C function
   */
  calculateBM1370Frequency(targetFreq: number): number | null {
    let fbDivider = 0;
    let postDivider1 = 0, postDivider2 = 0;
    let refDivider = 0;
    let minDifference = 10;
    const maxDiff = 1.0;
    let newFreq = 50.0;

    for (let refDivLoop = 2; refDivLoop > 0 && fbDivider === 0; refDivLoop--) {
      for (let postDiv1Loop = 7; postDiv1Loop > 0 && fbDivider === 0; postDiv1Loop--) {
        for (let postDiv2Loop = 7; postDiv2Loop > 0 && fbDivider === 0; postDiv2Loop--) {
          if (postDiv1Loop >= postDiv2Loop) {
            const tempFbDivider = Math.round((postDiv1Loop * postDiv2Loop * targetFreq * refDivLoop) / 25.0);

            if (tempFbDivider >= 0xa0 && tempFbDivider <= 0xef) {
              const tempFreq = 25.0 * tempFbDivider / (refDivLoop * postDiv2Loop * postDiv1Loop);
              const freqDiff = Math.abs(targetFreq - tempFreq);

              if (freqDiff < minDifference && freqDiff < maxDiff) {
                fbDivider = tempFbDivider;
                postDivider1 = postDiv1Loop;
                postDivider2 = postDiv2Loop;
                refDivider = refDivLoop;
                minDifference = freqDiff;
                newFreq = tempFreq;
              }
            }
          }
        }
      }
    }

    if (fbDivider === 0) {
      // console.warn( `PLL cannot synthesise ${targetFreq.toFixed(2)} MHz (≥1 MHz off everywhere)` );
      return null; 
    }

    return +newFreq.toFixed(2); 
  }

  /**
   * Calculate the actual frequency for BM1366 chip
   * Direct port of the C function
   */
  calculateBM1366Frequency(targetFreq: number): number | null {
    let fbDivider = 0;
    let postDivider1 = 0, postDivider2 = 0;
    let refDivider = 0;
    let minDifference = 10;
    let newFreq = 200.0;

    for (let refDivLoop = 2; refDivLoop > 0 && fbDivider === 0; refDivLoop--) {
      for (let postDiv1Loop = 7; postDiv1Loop > 0 && fbDivider === 0; postDiv1Loop--) {
        for (let postDiv2Loop = 1; postDiv2Loop < postDiv1Loop && fbDivider === 0; postDiv2Loop++) {
          const tempFbDivider = Math.round((postDiv1Loop * postDiv2Loop * targetFreq * refDivLoop) / 25.0);

          if (tempFbDivider >= 144 && tempFbDivider <= 235) {
            const tempFreq = 25.0 * tempFbDivider / (refDivLoop * postDiv2Loop * postDiv1Loop);
            const freqDiff = Math.abs(targetFreq - tempFreq);

            if (freqDiff < minDifference) {
              fbDivider = tempFbDivider;
              postDivider1 = postDiv1Loop;
              postDivider2 = postDiv2Loop;
              refDivider = refDivLoop;
              minDifference = freqDiff;
              newFreq = tempFreq;
              break;
            }
          }
        }
      }
    }

    if (fbDivider === 0) {
      // console.warn(`PLL cannot synthesise ${targetFreq.toFixed(2)} MHz on BM1366`);
      return null;
    }
    return +newFreq.toFixed(3);
  }

  /**
   * Calculate the actual frequency for BM1368 chip
   * Direct port of the C function
   */
  calculateBM1368Frequency(targetFreq: number): number | null {
    const maxDiff = 0.001;
    let postdivMin = 255;
    let postdiv2Min = 255;
    let bestFreq = 0;
    let bestRefDiv = 0, bestFbDiv = 0, bestPostDiv1 = 0, bestPostDiv2 = 0;
    let found = false;

    for (let refDiv = 2; refDiv > 0; refDiv--) {
      for (let postDiv1 = 7; postDiv1 > 0; postDiv1--) {
        for (let postDiv2 = 7; postDiv2 > 0; postDiv2--) {
          const fbDivider = Math.round(targetFreq / 25.0 * (refDiv * postDiv2 * postDiv1));
          const newFreq = 25.0 * fbDivider / (refDiv * postDiv2 * postDiv1);

          if (fbDivider >= 144 && fbDivider <= 235 &&
              Math.abs(targetFreq - newFreq) < maxDiff &&
              postDiv1 >= postDiv2 &&
              postDiv1 * postDiv2 < postdivMin &&
              postDiv2 <= postdiv2Min) {

            postdiv2Min = postDiv2;
            postdivMin = postDiv1 * postDiv2;
            bestFreq = newFreq;
            bestRefDiv = refDiv;
            bestFbDiv = fbDivider;
            bestPostDiv1 = postDiv1;
            bestPostDiv2 = postDiv2;
            found = true;
          }
        }
      }
    }

    if (!found) {
      // console.warn(`PLL cannot synthesise ${targetFreq.toFixed(2)} MHz on BM1368`);
      return null;
    }
    return +bestFreq.toFixed(3);
  }

  /**
   * Calculate the actual frequency for BM1397 chip
   * Direct port of the C function
   */
  calculateBM1397Frequency(targetFreq: number): number {
    const FREQ_MULT = 25.0;
    const defFreq = 200.0;
    
    let f1 = targetFreq;
    if (f1 < 50) {
      f1 = 50;
    } else if (f1 > 650) {
      f1 = 650;
    }

    let fb = 2;
    let fc1 = 1;
    let fc2 = 5;
    
    if (f1 >= 500) {
      fb = 1;
    } else if (f1 <= 150) {
      fc1 = 3;
    } else if (f1 <= 250) {
      fc1 = 2;
    }

    const baseFreq = FREQ_MULT * Math.ceil(f1 * fb * fc1 * fc2 / FREQ_MULT);

    const fa = baseFreq / FREQ_MULT;
    const faMin = 0x10;
    const faMax = 0x104;

    if (fa < faMin || fa > faMax) {
      return defFreq;
    } else {
      const newFreq = baseFreq / (fb * fc1 * fc2);
      return parseFloat(newFreq.toFixed(2));
    }
  }

  private buildRamp(raw: number[]): number[] {
    return Array.from(
      new Set(
        raw
          .map(f => this.calculateActualFrequency(f))
          .filter(f => f !== null)
          .map(f => Math.floor(f))
          .filter((f, i, arr) => arr.indexOf(f) === i)
          .sort((a, b) => a - b)
      )
    );
  }

  /** Sorted, cached frequency ramp for current model */
  private get sortedFreqs(): number[] {
    let ramp = this.sortedFreqCache.get(this.ASICModel);
    if (!ramp) {
      ramp = this.buildRamp(this.validFrequencies[this.ASICModel]);
      this.sortedFreqCache.set(this.ASICModel, ramp);
    }
    return ramp;
  }

  /** Clamp helper */
  private clamp(v: number, lo: number, hi: number): number {
    return v < lo ? lo : v > hi ? hi : v;
  }

  /** Binary search: first index ≥ value */
  private lowerBound(arr: number[], value: number): number {
    let lo = 0, hi = arr.length;
    while (lo < hi) {
      const mid = (lo + hi) >>> 1;
      arr[mid] < value ? (lo = mid + 1) : (hi = mid);
    }
    return lo;
  }

  private deriveFloat(current: number): number {
    const ramp = this.sortedFreqs;
    
    // Group frequencies by their integer value (what would be stored in NVS)
    const freqsByIntValue = new Map<number, number[]>();
    
    ramp.forEach(freq => {
      const intValue = Math.floor(freq);
      if (!freqsByIntValue.has(intValue)) {
        freqsByIntValue.set(intValue, []);
      }
      freqsByIntValue.get(intValue)!.push(freq);
    });
    
    // Log each group that has multiple frequencies
    freqsByIntValue.forEach((freqs, intValue) => {
      if (freqs.length > 1) {
        console.debug(`Integer ${intValue} has ${freqs.length} frequencies: ${freqs.join(', ')}`);
      }
    });
    
    // Find frequencies with the same integer value, which would be duplicates in NVS
    const nvsDuplicates: number[] = [];
    freqsByIntValue.forEach((freqs, intValue) => {
      if (freqs.length > 1) {
        // Add all but the first frequency to the duplicates list
        nvsDuplicates.push(...freqs.slice(1));
      }
    });
    
    if (nvsDuplicates.length > 0) {
      // Only show first 50 duplicates to keep the log manageable
      const displayCount = Math.min(50, nvsDuplicates.length);
      console.warn(`Found ${nvsDuplicates.length} frequencies that will be duplicates when stored in NVS. First ${displayCount}: ${nvsDuplicates.slice(0, displayCount).join(', ')}. These have the same integer part as other frequencies.`);
    }
    
    // Find exact match by value if possible
    const exactIndex = ramp.indexOf(current);
    if (exactIndex !== -1) {
      return ramp[exactIndex];
    }
    
    // Fallback to finding the first frequency with the same integer value
    const flooredCurrent = Math.floor(current);
    const matchingFreqs = freqsByIntValue.get(flooredCurrent);
    
    return matchingFreqs?.[0] ?? current;
  }


  /** Walk one slot up or down the ramp (for the "±" buttons). */
  private stepByIndex(current: number, step: 1 | -1): number {
    const ramp = this.sortedFreqs;
    if (!ramp.length || !step) return current;

    let i = this.lowerBound(ramp, current);

    if (i < ramp.length && ramp[i] === current) {
      i += step;
    } else if (step < 0) {
      i -= 1;
    }

    return i >= 0 && i < ramp.length ? ramp[i] : current;
  }


  /** Move by ≈ delta MHz, then snap to the closest ramp value on the correct side. */
  private stepByDelta(current: number, deltaMHz: number): number {
    const ramp = this.sortedFreqs;
    if (!ramp.length || deltaMHz === 0) return current;

    const target = current + deltaMHz;
    const dir    = Math.sign(deltaMHz);

    let hi = this.lowerBound(ramp, target);
    let lo = hi - 1;

    let idx: number;
    if (lo < 0) {
      idx = hi;
    } else if (hi >= ramp.length) {
      idx = lo;
    } else {
      idx = Math.abs(ramp[lo] - target) <= Math.abs(ramp[hi] - target) ? lo : hi;
    }

    if (dir > 0 && ramp[idx] <= current && idx + 1 < ramp.length) idx++;
    if (dir < 0 && ramp[idx] >= current && idx - 1 >= 0) idx--;

    return ramp[idx] ?? current;
  }

  /** Public entry point – now blissfully short */
  incrementValue(controlName: string, amount: number): void {
    const control = this.form.get(controlName);
    if (!control || !amount) return;
  
    const current = Number(control.value) || 0;
    let next = current;
  
    if (controlName === 'frequency') {
      if(amount === 1 || amount === -1) {
        next = this.stepByIndex(current, amount);
      } else {
        next = this.stepByDelta(current, amount);
      }
      next = this.clamp(
        next,
        this.minFrequency[this.ASICModel],
        this.maxFrequency[this.ASICModel],
      );
    } else if (controlName === 'coreVoltage') {
      next = this.clamp(
        current + amount,
        this.minVoltage[this.ASICModel],
        this.maxVoltage[this.ASICModel],
      );
    } else {
      next = current + amount;
    }
  
    if (next === current) return;
  
    this.form.patchValue({ [controlName]: next });
    control.markAsDirty();
    this.form.markAsDirty();
  }
  
  private getValueColor(controlName: string, currentValue: number | null): string {
    if (!currentValue) return 'var(--primary-color)';
    
    const defaultValue = controlName === 'frequency' ? 
      this.defaultFrequency[this.ASICModel] : 
      this.defaultVoltage[this.ASICModel];
      
    const maxValue = controlName === 'frequency' ?
      this.maxFrequency[this.ASICModel] :
      this.maxVoltage[this.ASICModel];
      
    const minValue = controlName === 'frequency' ?
      this.minFrequency[this.ASICModel] :
      this.minVoltage[this.ASICModel];
    
    if (currentValue === defaultValue) {
      return '#22c55e';
    } else if (currentValue < defaultValue) {
      const percentage = (currentValue - minValue) / (defaultValue - minValue);
      return this.interpolateColor('#3b82f6', '#22c55e', percentage);
    } else {
      const percentage = (currentValue - defaultValue) / (maxValue - defaultValue);
      return this.interpolateColor('#22c55e', '#ef4444', percentage);
    }
  }
  
  getFrequencyColor(): string {
    return this.getValueColor('frequency', this.form?.get('frequency')?.value);
  }
  
  getVoltageColor(): string {
    return this.getValueColor('coreVoltage', this.form?.get('coreVoltage')?.value);
  }
  
  private interpolateColor(color1: string, color2: string, percentage: number): string {
    const clampedPercentage = Math.max(0, Math.min(1, percentage));
    
    const r1 = parseInt(color1.substring(1, 3), 16);
    const g1 = parseInt(color1.substring(3, 5), 16);
    const b1 = parseInt(color1.substring(5, 7), 16);
    
    const r2 = parseInt(color2.substring(1, 3), 16);
    const g2 = parseInt(color2.substring(3, 5), 16);
    const b2 = parseInt(color2.substring(5, 7), 16);
    
    const r = Math.round(r1 + (r2 - r1) * clampedPercentage);
    const g = Math.round(g1 + (g2 - g1) * clampedPercentage);
    const b = Math.round(b1 + (b2 - b1) * clampedPercentage);
    
    return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`;
  }

  
  /**
   * Create a default preset with factory values
   */
  private createDefaultPreset(): OverclockPreset {
    return {
      name: `Factory Default (${this.ASICModel})`,
      frequency: this.defaultFrequency[this.ASICModel],
      coreVoltage: this.defaultVoltage[this.ASICModel],
      timestamp: 0,
      asicModel: this.ASICModel,
      builtIn: true
    };
  }

  /**
   * Load all saved presets from localStorage
   */
  loadPresets(): void {
    try {
      const presetsJson = localStorage.getItem('overclockPresets');
      if (presetsJson) {
        this.presets = JSON.parse(presetsJson);
        this.presets = this.presets.filter(preset => preset.asicModel === this.ASICModel);
      } else {
        this.presets = [];
      }
      this.presets.unshift(this.createDefaultPreset());
      this.sortPresets()
    } catch (error) {
      console.error('Error loading presets from localStorage', error);
      this.toastr.error('Failed to load saved presets', 'Error');

      this.presets = [this.createDefaultPreset()];
    }
  }
  
  /**
   * Save all presets to localStorage
   */
  savePresetsToStorage(): void {
    try {
      let allPresets: OverclockPreset[] = [];
      const presetsJson = localStorage.getItem('overclockPresets');
      if (presetsJson) {
        allPresets = JSON.parse(presetsJson);
        allPresets = allPresets.filter(preset => preset.asicModel !== this.ASICModel);
      }
      
      const userPresets = this.presets.filter(preset => !preset.builtIn);
      allPresets = [...allPresets, ...userPresets];
      
      localStorage.setItem('overclockPresets', JSON.stringify(allPresets));
    } catch (error) {
      console.error('Error saving presets to localStorage', error);
      this.toastr.error('Failed to save presets', 'Error');
    }
  }
  
  public get selectedPreset(): OverclockPreset | null {
    if (!this.form) return null;
    
    const currentFrequency = this.form.get('frequency')?.value;
    const currentVoltage = this.form.get('coreVoltage')?.value;
    
    if (!currentFrequency || !currentVoltage) return null;
    
    return this.presets.find(preset => 
      preset.frequency === currentFrequency && 
      preset.coreVoltage === currentVoltage
    ) || null;
  }
  
  public get hasMatchingPreset(): boolean {
    return this.selectedPreset !== null;
  }
  
  public editPresetName(preset: OverclockPreset, event: Event): void {
    event.stopPropagation();
    
    if (preset.builtIn) {
      this.toastr.error(`Cannot edit built-in preset "${preset.name}"`, 'Error');
      return;
    }
    
    this.editingPreset = preset;
    this.presetName = preset.name;
    this.showPresetDialog = true;
  }
  
  /**
   * Open dialog to save the current settings as a preset
   */
  openSavePresetDialog(): void {
    this.presetName = '';
    this.editingPreset = null;
    this.showPresetDialog = true;
  }
  
  /**
   * Save current settings as a new preset or update existing
   */
  savePreset(): void {
    if (!this.presetName.trim()) {
      this.toastr.error('Please enter a name for the preset', 'Error');
      return;
    }
    const formValues = this.form.getRawValue();
    if (this.editingPreset) {
      this.editingPreset.name = this.presetName.trim();
      this.toastr.success(`Preset name updated to "${this.presetName}"`, 'Success');
    } else {
      const newPreset: OverclockPreset = {
        name: this.presetName.trim(),
        frequency: formValues.frequency,
        coreVoltage: formValues.coreVoltage,
        timestamp: Date.now(),
        asicModel: this.ASICModel
      };
      
      const existingIndex = this.presets.findIndex(p => p.name === newPreset.name);
      if (existingIndex >= 0) {
        this.presets[existingIndex] = newPreset;
      } else {
        this.presets.push(newPreset);
      }
      
      this.toastr.success(`Preset "${this.presetName}" saved successfully`, 'Success');
    }
    this.sortPresets();
    this.savePresetsToStorage();
    this.showPresetDialog = false;
    this.editingPreset = null;
  }
  
  /**
   * Sort presets with built-ins at top, then by timestamp
   */
  private sortPresets(): void {
    this.presets.sort((a, b) => {
      if (a.builtIn && !b.builtIn) return -1;
      if (!a.builtIn && b.builtIn) return 1;
      return b.timestamp - a.timestamp;
    });
  }
  
  /**
   * Check if preset matches current values
   */
  public presetMatchesCurrentValues(preset: OverclockPreset): boolean {
    if (!this.form) return false;
    
    const currentFrequency = this.form.get('frequency')?.value;
    const currentVoltage = this.form.get('coreVoltage')?.value;
    
    return preset.frequency === currentFrequency && 
           preset.coreVoltage === currentVoltage;
  }

  /**
   * Acknowledge the warning message and save to localStorage
   */
  acknowledgeWarning(): void {
    this.warningAcknowledged = true;
    localStorage.setItem(this.STORAGE_KEY_WARNING, 'true');
  }

  /**
   * Apply settings from a preset
   */
  applyPreset(preset: OverclockPreset): void {
    this.form.patchValue({
      frequency: preset.frequency,
      coreVoltage: preset.coreVoltage
    });
    
    this.form.markAsDirty();
    
    this.toastr.info(`Applied preset "${preset.name}"`, 'Preset Applied');
  }
  
  /**
   * Delete a preset
   */
  deletePreset(preset: OverclockPreset, event: Event): void {
    event.stopPropagation();
    if (preset.builtIn) {
      this.toastr.error(`Cannot delete built-in preset "${preset.name}"`, 'Error');
      return;
    }
    
    this.presets = this.presets.filter(p => p.name !== preset.name);
    this.savePresetsToStorage();
    this.toastr.success(`Deleted preset "${preset.name}"`, 'Success');
  }
  
  /**
   * Close the preset dialog without saving
   */
  cancelSavePreset(): void {
    this.showPresetDialog = false;
    this.editingPreset = null;
  }

  /**
   * Determine if the actual frequency indicator should be shown
   * Handles floating point precision issues in comparison
   */
  shouldShowFrequencyIndicator(): boolean {
    const inputFreq = this.form?.get('frequency')?.value;
    if (!inputFreq) {
      return false;
    }
    
    const actualFreq = this.actualFrequency;
    
    const epsilon = 0.01;
    const diff = Math.abs(inputFreq - actualFreq);
    
    return diff > epsilon;
  }

  /**
   * Find the next valid frequency in the sequence
   */
  findNextValidFrequency(current: number, increment: boolean): number {
    const validFreqs = this.validFrequencies[this.ASICModel];
    if (!validFreqs || validFreqs.length === 0) {
      return increment ? current + 1 : current - 1;
    }
    
    if (increment) {
      const next = validFreqs.find(freq => freq > current);
      return next !== undefined ? next : current;
    } else {
      const prev = [...validFreqs].reverse().find(freq => freq < current);
      return prev !== undefined ? prev : current;
    }
  }

  public updateSystem() {
    console.log('Form dirty state before submission:', this.form.dirty);

    const form = this.form.getRawValue();

    if (form.stratumPassword === '*****') {
      delete form.stratumPassword;
    }

    const formData = {
      ...form,
      flipscreen: form.flipscreen === true ? 1 : 0,
      invertscreen: form.invertscreen === true ? 1 : 0,
      autofanspeed: form.autofanspeed === true ? 1 : 0,
    };

    this.systemService.updateSystem(this.uri, formData)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Saved settings for ${this.uri}` : 'Saved settings';
          this.toastr.success(successMessage, 'Success!');
          this.savedChanges = true;
          
          const currentValues = this.form.getRawValue();
          this.form.reset(currentValues);
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Could not save settings for ${this.uri}. ${err.message}` : `Could not save settings. ${err.message}`;
          this.toastr.error(errorMessage, 'Error');
          this.savedChanges = false;
        }
      });
  }

  showWifiPassword: boolean = false;
  toggleWifiPasswordVisibility() {
    this.showWifiPassword = !this.showWifiPassword;
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
          const successMessage = this.uri ? `Bitaxe at ${this.uri} restarted` : 'Bitaxe restarted';
          this.toastr.success(successMessage, 'Success');
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Failed to restart device at ${this.uri}. ${err.message}` : `Failed to restart device. ${err.message}`;
          this.toastr.error(errorMessage, 'Error');
        }
      });
  }

  getDropdownFrequency() {
    let options = [];
    switch(this.ASICModel) {
        case this.eASICModel.BM1366: options = [...this.BM1366DropdownFrequency]; break;
        case this.eASICModel.BM1368: options = [...this.BM1368DropdownFrequency]; break;
        case this.eASICModel.BM1370: options = [...this.BM1370DropdownFrequency]; break;
        case this.eASICModel.BM1397: options = [...this.BM1397DropdownFrequency]; break;
        default: return [];
    }

    const currentFreq = this.form?.get('frequency')?.value;

    if (currentFreq && !options.some(opt => opt.value === currentFreq)) {
        options.push({
            name: `${currentFreq} (Custom)`,
            value: currentFreq
        });
        options.sort((a, b) => a.value - b.value);
    }

    return options;
  }

  getCoreVoltage() {
    let options = [];
    switch(this.ASICModel) {
        case this.eASICModel.BM1366: options = [...this.BM1366CoreVoltage]; break;
        case this.eASICModel.BM1368: options = [...this.BM1368CoreVoltage]; break;
        case this.eASICModel.BM1370: options = [...this.BM1370CoreVoltage]; break;
        case this.eASICModel.BM1397: options = [...this.BM1397CoreVoltage]; break;
        default: return [];
    }

    const currentVoltage = this.form?.get('coreVoltage')?.value;

    if (currentVoltage && !options.some(opt => opt.value === currentVoltage)) {
        options.push({
            name: `${currentVoltage} (Custom)`,
            value: currentVoltage
        });
        options.sort((a, b) => a.value - b.value);
    }

    return options;
  }
}
