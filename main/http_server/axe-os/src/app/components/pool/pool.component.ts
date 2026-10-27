import { HttpErrorResponse } from '@angular/common/http';
import { getHttpErrorMessage } from 'src/app/utils/error-handler';
import { Component, Input, OnInit, ChangeDetectorRef } from '@angular/core';
import { FormBuilder, FormGroup, FormArray, Validators, ValidatorFn, ValidationErrors, AbstractControl, FormsModule, ReactiveFormsModule } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemApiService } from 'src/app/services/system.service';
import { LiveDataService } from 'src/app/services/live-data.service';
import { first } from 'rxjs';
import { TooltipDirective } from '../../directives/tooltip.directive';
import { TooltipTextIconComponent } from '../tooltip-text-icon/tooltip-text-icon.component';
import { RadioButtonComponent } from '../radio-button/radio-button.component';
import { CheckboxComponent } from '../checkbox/checkbox.component';

interface ITlsOption {
  value: number;
  label: string;
}

interface IProtocolOption {
  value: 'SV1' | 'SV2';
  label: string;
}

interface IChannelOption {
  value: 'standard' | 'extended';
  label: string;
}

@Component({
  selector: 'app-pool',
  templateUrl: './pool.component.html',
  imports: [FormsModule, ReactiveFormsModule, TooltipDirective, TooltipTextIconComponent, RadioButtonComponent, CheckboxComponent]
})
export class PoolComponent implements OnInit {
  public form!: FormGroup;

  private pendingDeletePoolIds: number[] = [];

  public readonly DEFAULT_BITCOIN_ADDRESS = 'bc1qnp980s5fpp8l94p5cvttmtdqy8rvrq74qly2yrfmzkdsntqzlc5qkc4rkq';

  public showPassword: boolean[] = [];
  public showAdvancedOptions: boolean[] = [];
  public activeCardIndex: number | null = 0;

  public tlsOptions: ITlsOption[] = [
    { value: 0, label: 'No TLS' },
    { value: 1, label: 'TLS (System certificate)' },
    { value: 2, label: 'TLS (Custom CA certificate)' }
  ];

  public protocolOptions: IProtocolOption[] = [
    { value: 'SV1', label: 'Stratum V1' },
    { value: 'SV2', label: 'Stratum V2' }
  ];

  public sv2ChannelOptions: IChannelOption[] = [
    { value: 'extended', label: 'Extended Channels' },
    { value: 'standard', label: 'Standard Channels' }
  ];

  public asicModel: string = '';

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemApiService,
    private liveDataService: LiveDataService,
    private toastr: ToastrService,
    private loadingService: LoadingService,
    private cdr: ChangeDetectorRef
  ) { }

  ngOnInit(): void {
    this.liveDataService.info$
      .pipe(first(), this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.asicModel = info.ASICModel || '';

        const poolsList = [...(info.pools || [])];
        const orderedList: any[] = poolsList.length > 0 ? poolsList : [
          {
            id: 0,
            stratumProtocol: 'SV1',
            stratumURL: '',
            stratumPort: 3333,
            stratumUser: '',
            stratumPassword: '',
            stratumSuggestedDifficulty: 0,
            stratumExtranonceSubscribe: false,
            stratumTLS: 0,
            stratumCert: '',
            stratumDecodeCoinbase: true,
            stratumShareWarning: true,
            stratumV2ChannelType: 'extended',
            stratumV2AuthorityPubkey: '',
            stratumV2RequireAuth: false
          }
        ];

        orderedList.forEach((pool, idx) => {
          pool.id = idx;
        });

        const poolsFormGroups = orderedList.map((pool: any) => {
          this.showPassword[pool.id] = false;
          this.showAdvancedOptions[pool.id] = false;

          return this.fb.group({
            id: [pool.id],
            stratumProtocol: [pool.stratumProtocol || 'SV1'],
            stratumURL: [pool.stratumURL || '', [
              Validators.required,
              Validators.pattern(/^(?!.*stratum\+tcp:\/\/)(?!.*:[1-9]\d{0,4}$).*$/),
            ]],
            stratumPort: [pool.stratumPort || 3333, [
              Validators.required,
              Validators.pattern(/^[^:]*$/),
              Validators.min(0),
              Validators.max(65535)
            ]],
            stratumUser: [pool.stratumUser || '', [Validators.required]],
            stratumPassword: [pool.stratumPassword || '*****'],
            stratumSuggestedDifficulty: [pool.stratumSuggestedDifficulty || 0, [Validators.required]],
            stratumExtranonceSubscribe: [pool.stratumExtranonceSubscribe == true, [Validators.required]],
            stratumTLS: [pool.stratumTLS || 0],
            stratumCert: [pool.stratumCert || ''],
            stratumDecodeCoinbase: [pool.stratumDecodeCoinbase == true, [Validators.required]],
            stratumShareWarning: [pool.stratumShareWarning == true, [Validators.required]],
            stratumV2ChannelType: [pool.stratumV2ChannelType || 'extended'],
            stratumV2AuthorityPubkey: [pool.stratumV2AuthorityPubkey || '', [this.base58Validator()]],
            stratumV2RequireAuth: [pool.stratumV2RequireAuth == true]
          });
        });

        this.form = this.fb.group({
          primaryPoolIndex: [0, [Validators.required]],
          secondaryPoolIndex: [orderedList.length > 1 ? 1 : 0, [Validators.required]],
          pools: this.fb.array(poolsFormGroups)
        });

        for (let i = 0; i < poolsFormGroups.length; i++) {
          this.setupTlsValidationForIndex(i);
        }

              });
  }

  get poolsArray(): FormArray {
    return this.form?.get('pools') as FormArray;
  }


  setupTlsValidationForIndex(index: number) {
    const poolGroup = this.poolsArray.at(index) as FormGroup;
    poolGroup.get('stratumTLS')?.valueChanges.subscribe(value => {
      const certControl = poolGroup.get('stratumCert');
      if (value === 2) {
        certControl?.setValidators([
          Validators.required,
          this.pemCertificateValidator()
        ]);
      } else {
        certControl?.clearValidators();
      }
      certControl?.updateValueAndValidity();
    });
    poolGroup.get('stratumTLS')?.updateValueAndValidity();
  }

  toggleCard(index: number) {
    this.activeCardIndex = this.activeCardIndex === index ? null : index;
  }

  addPool() {
    if (this.poolsArray.length < 8) {
      const nextId = this.poolsArray.length;
      const poolGroup = this.fb.group({
        id: [nextId],
        stratumProtocol: ['SV1'],
        stratumURL: ['', [
          Validators.required,
          Validators.pattern(/^(?!.*stratum\+tcp:\/\/)(?!.*:[1-9]\d{0,4}$).*$/),
        ]],
        stratumPort: [3333, [
          Validators.required,
          Validators.pattern(/^[^:]*$/),
          Validators.min(0),
          Validators.max(65535)
        ]],
        stratumUser: ['', [Validators.required]],
        stratumPassword: [''],
        stratumSuggestedDifficulty: [0, [Validators.required]],
        stratumExtranonceSubscribe: [false, [Validators.required]],
        stratumTLS: [0],
        stratumCert: [''],
        stratumDecodeCoinbase: [true, [Validators.required]],
        stratumShareWarning: [true, [Validators.required]],
        stratumV2ChannelType: ['extended'],
        stratumV2AuthorityPubkey: ['', [this.base58Validator()]],
        stratumV2RequireAuth: [false]
      });

      this.poolsArray.push(poolGroup);
      this.showPassword[nextId] = false;
      this.showAdvancedOptions[nextId] = false;

      this.setupTlsValidationForIndex(nextId);
      this.activeCardIndex = nextId; // Expand newly added pool

      this.form.get('primaryPoolIndex')?.setValue(0, { emitEvent: false });
      this.form.get('secondaryPoolIndex')?.setValue(this.poolsArray.length > 1 ? 1 : 0, { emitEvent: false });
      this.form.markAsDirty();
    }
  }

  deletePool(index: number) {
    if (this.poolsArray.length <= 1) {
      this.toastr.error('Cannot delete the only configured pool.');
      return;
    }

    const poolGroup = this.poolsArray.at(index);
    if (!poolGroup) return;

    const oldTotal = this.poolsArray.length;
    this.poolsArray.removeAt(index);
    this.pendingDeletePoolIds.push(oldTotal - 1);

    this.poolsArray.controls.forEach((ctrl, newIdx) => {
      ctrl.get('id')?.setValue(newIdx, { emitEvent: false });
    });

    this.form.get('primaryPoolIndex')?.setValue(0, { emitEvent: false });
    this.form.get('secondaryPoolIndex')?.setValue(this.poolsArray.length > 1 ? 1 : 0, { emitEvent: false });

    this.form.markAsDirty();

    if (this.activeCardIndex === index) {
      this.activeCardIndex = this.poolsArray.length > 0 ? 0 : null;
    } else if (this.activeCardIndex !== null && this.activeCardIndex > index) {
      this.activeCardIndex--;
    }
  }

  isDeleteDisabled(): boolean {
    return this.poolsArray?.length <= 1;
  }

  public updateSystem() {
    const form = this.form.getRawValue();

    this.systemService.updateSystem(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const deleteRequests = this.pendingDeletePoolIds.map(id => this.systemService.deletePool(this.uri, id));
          if (deleteRequests.length > 0) {
            import('rxjs').then(({ forkJoin }) => {
              forkJoin(deleteRequests)
                .pipe(this.loadingService.lockUIUntilComplete())
                .subscribe({
                  next: () => {
                    this.onSaveSuccess();
                  },
                  error: (err: HttpErrorResponse) => {
                    this.toastr.error(`Saved settings, but failed to delete cleared pools: ${err.message}`);
                    this.onSaveSuccess();
                  }
                });
            });
          } else {
            this.onSaveSuccess();
          }
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error(`Could not save pool settings. ${getHttpErrorMessage(err, this.uri)}`);
        }
      });
  }

  private onSaveSuccess() {
    const successMessage = this.uri ? `Saved pool settings for ${this.uri}` : 'Saved pool settings';
    this.toastr.success(successMessage);
    this.pendingDeletePoolIds = [];
    this.form.markAsPristine();
  }

  private extractPort(url: string): { cleanUrl: string, port?: number } {
    const match = url.match(/:(\d{1,5})$/);
    if (match) {
      const port = parseInt(match[1], 10);
      return { cleanUrl: url.slice(0, match.index), port };
    }
    return { cleanUrl: url };
  }

  public onUrlChange(index: number) {
    const poolGroup = this.poolsArray.at(index);
    if (!poolGroup) return;

    const urlControl = poolGroup.get('stratumURL');
    const portControl = poolGroup.get('stratumPort');
    const tlsControl = poolGroup.get('stratumTLS');
    if (!urlControl || !portControl || !tlsControl) return;

    let urlValue = urlControl.value?.trim() || '';
    if (!urlValue) return;

    const prefixes = [
      { prefix: 'stratum+tcp://', tlsMode: false },
      { prefix: 'stratum+tls://', tlsMode: true },
      { prefix: 'stratum+ssl://', tlsMode: true }
    ] as const;

    const matched = prefixes.find(({ prefix }) => urlValue.startsWith(prefix));
    if (matched) {
      urlValue = urlValue.slice(matched.prefix.length);
      tlsControl.setValue(+matched.tlsMode);
    }

    const { cleanUrl, port } = this.extractPort(urlValue);

    if (port !== undefined) {
      portControl.setValue(port);
    }
    urlControl.setValue(cleanUrl);
    urlControl.markAsDirty();
  }

  onCertFileSelected(event: Event, index: number): void {
    const fileInput = event.target as HTMLInputElement;

    if (fileInput.files && fileInput.files.length > 0) {
      const file = fileInput.files[0];
      const reader = new FileReader();

      reader.onload = () => {
        const fileContent = reader.result as string;
        const poolGroup = this.poolsArray.at(index);
        poolGroup.get('stratumCert')?.setValue(fileContent);
        poolGroup.get('stratumCert')?.markAsDirty();

        fileInput.value = '';
      };

      reader.onerror = () => {
        this.toastr.error('Failed to read certificate file');
        fileInput.value = '';
      };

      reader.readAsText(file);
    }
  }

  private pemCertificateValidator(): ValidatorFn {
    return (control: AbstractControl): ValidationErrors | null => {
      const value = control.value?.trim();
      if (!value) return null;

      const pemChainRegex =
        /^(?:-----BEGIN CERTIFICATE-----[\s\S]*?-----END CERTIFICATE-----\s*)+$/;

      return pemChainRegex.test(value) ? null : { invalidCertificate: true };
    };
  }

  private base58Validator(): ValidatorFn {
    return (control: AbstractControl): ValidationErrors | null => {
      const value = control.value?.trim();
      if (!value) return null;

      const base58Regex = /^[123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz]+$/;
      if (!base58Regex.test(value)) {
        return { invalidBase58: true };
      }

      if (value.length < 40 || value.length > 52) {
        return { invalidBase58Length: true };
      }

      return null;
    };
  }

  trackByFn(index: number, option: { value: string | number }): string | number {
    return option.value;
  }

  isUsingDefaultAddress(index: number): boolean {
    if (!this.poolsArray) return false;
    const poolGroup = this.poolsArray.at(index);
    const userValue = poolGroup?.get('stratumUser')?.value || '';
    return userValue.includes(this.DEFAULT_BITCOIN_ADDRESS);
  }

  isAnyPoolUsingDefaultAddress(): boolean {
    if (!this.form || !this.poolsArray) return false;
    for (let i = 0; i < this.poolsArray.length; i++) {
      if (this.isUsingDefaultAddress(i)) return true;
    }
    return false;
  }

  isPoolV2Enabled(index: number): boolean {
    if (!this.poolsArray) return false;
    const poolGroup = this.poolsArray.at(index);
    return poolGroup?.get('stratumProtocol')?.value === 'SV2';
  }

  isPoolV2Extended(index: number): boolean {
    if (!this.isPoolV2Enabled(index)) return false;
    const poolGroup = this.poolsArray.at(index);
    return poolGroup?.get('stratumV2ChannelType')?.value === 'extended';
  }

  hasAdvancedOptions(index: number): boolean {
    return !this.isPoolV2Enabled(index) || this.isPoolV2Extended(index);
  }

  getProtocolLabel(value: string): string {
    const option = this.protocolOptions.find(opt => opt.value === value);
    return option ? option.label : value;
  }

  movePoolUp(index: number): void {
    if (index > 0) {
      this.movePool(index, index - 1);
    }
  }

  movePoolDown(index: number): void {
    if (index < this.poolsArray.length - 1) {
      this.movePool(index, index + 1);
    }
  }

  movePool(fromIndex: number, toIndex: number): void {
    if (fromIndex < 0 || fromIndex >= this.poolsArray.length ||
        toIndex < 0 || toIndex >= this.poolsArray.length ||
        fromIndex === toIndex) {
      return;
    }

    const groupA = this.poolsArray.at(fromIndex);
    const groupB = this.poolsArray.at(toIndex);

    const valA = { ...groupA.getRawValue() };
    const valB = { ...groupB.getRawValue() };

    valB.id = fromIndex;
    valA.id = toIndex;

    groupA.setValue(valB);
    groupB.setValue(valA);

    const showPwA = this.showPassword[fromIndex] ?? false;
    const showPwB = this.showPassword[toIndex] ?? false;
    this.showPassword[fromIndex] = showPwB;
    this.showPassword[toIndex] = showPwA;

    const showAdvA = this.showAdvancedOptions[fromIndex] ?? false;
    const showAdvB = this.showAdvancedOptions[toIndex] ?? false;
    this.showAdvancedOptions[fromIndex] = showAdvB;
    this.showAdvancedOptions[toIndex] = showAdvA;

    if (this.activeCardIndex === fromIndex) {
      this.activeCardIndex = toIndex;
    } else if (this.activeCardIndex === toIndex) {
      this.activeCardIndex = fromIndex;
    }

    this.form.get('primaryPoolIndex')?.setValue(0, { emitEvent: false });
    this.form.get('secondaryPoolIndex')?.setValue(this.poolsArray.length > 1 ? 1 : 0, { emitEvent: false });
    this.form.markAsDirty();
    this.cdr.detectChanges();
  }
}
