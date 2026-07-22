import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, FormArray, Validators, ValidatorFn, ValidationErrors, AbstractControl } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemApiService } from 'src/app/services/system.service';
import { LiveDataService } from 'src/app/services/live-data.service';
import { first } from 'rxjs';

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

interface IPoolDropdownOption {
  value: number;
  label: string;
}

@Component({
    selector: 'app-pool',
    templateUrl: './pool.component.html',
    standalone: false
})
export class PoolComponent implements OnInit {
  public form!: FormGroup;
  public savedChanges: boolean = false;

  private previousPrim: number = 0;
  private previousSec: number = 1;
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
    private loadingService: LoadingService
  ) { }

  ngOnInit(): void {
    this.liveDataService.info$
      .pipe(first(), this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.asicModel = info.ASICModel || '';

        const poolsList = [...(info.pools || [])];
        
        // Ensure primary pool index slot is in the list
        if (!poolsList.some(p => p.id === info.primaryPoolIndex)) {
          poolsList.push({
            id: info.primaryPoolIndex,
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
            stratumV2ChannelType: 'extended',
            stratumV2AuthorityPubkey: ''
          });
        }
        
        // Ensure secondary pool index slot is in the list
        if (!poolsList.some(p => p.id === info.secondaryPoolIndex)) {
          poolsList.push({
            id: info.secondaryPoolIndex,
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
            stratumV2ChannelType: 'extended',
            stratumV2AuthorityPubkey: ''
          });
        }

        // Sort by ID to keep the card order ascending
        poolsList.sort((a, b) => (a.id ?? 0) - (b.id ?? 0));

        const poolsFormGroups = poolsList.map((pool: any) => {
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
            stratumV2ChannelType: [pool.stratumV2ChannelType || 'extended'],
            stratumV2AuthorityPubkey: [pool.stratumV2AuthorityPubkey || '', [this.base58Validator()]]
          });
        });

        this.form = this.fb.group({
          primaryPoolIndex: [info.primaryPoolIndex, [Validators.required]],
          secondaryPoolIndex: [info.secondaryPoolIndex, [Validators.required]],
          pools: this.fb.array(poolsFormGroups)
        });

        for (let i = 0; i < poolsFormGroups.length; i++) {
          this.setupTlsValidationForIndex(i);
        }

        this.previousPrim = info.primaryPoolIndex;
        this.previousSec = info.secondaryPoolIndex;

        this.updatePoolDropdownOptions();
        this.poolsArray.valueChanges.subscribe(() => {
          this.updatePoolDropdownOptions();
        });

        this.form.get('primaryPoolIndex')?.valueChanges.subscribe(primVal => {
          const secVal = this.form.get('secondaryPoolIndex')?.value;
          if (primVal === secVal) {
            this.form.get('secondaryPoolIndex')?.setValue(this.previousPrim, { emitEvent: false });
            this.previousSec = this.previousPrim;
          }
          this.previousPrim = primVal;
        });

        this.form.get('secondaryPoolIndex')?.valueChanges.subscribe(secVal => {
          const primVal = this.form.get('primaryPoolIndex')?.value;
          if (secVal === primVal) {
            this.form.get('primaryPoolIndex')?.setValue(this.previousSec, { emitEvent: false });
            this.previousSec = secVal;
          }
          this.previousSec = secVal;
        });
      });
  }

  get poolsArray(): FormArray {
    return this.form?.get('pools') as FormArray;
  }

  public poolDropdownOptions: IPoolDropdownOption[] = [];

  public updatePoolDropdownOptions(): void {
    if (!this.poolsArray) {
      this.poolDropdownOptions = [];
      return;
    }
    const newOptions = this.poolsArray.controls.map((control) => {
      const id = control.get('id')?.value;
      const url = control.get('stratumURL')?.value || '';
      const proto = control.get('stratumProtocol')?.value || 'SV1';
      const urlDisplay = url ? `${url} (${proto})` : '(not configured)';
      return {
        value: id,
        label: `Pool ${id + 1}: ${urlDisplay}`
      };
    });

    if (JSON.stringify(newOptions) !== JSON.stringify(this.poolDropdownOptions)) {
      this.poolDropdownOptions = newOptions;
    }
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

  private getFirstAvailableId(): number {
    const existingIds = this.poolsArray.controls.map(c => c.get('id')?.value);
    for (let i = 0; i < 8; i++) {
      if (!existingIds.includes(i)) {
        return i;
      }
    }
    return 0;
  }

  toggleCard(index: number) {
    this.activeCardIndex = this.activeCardIndex === index ? null : index;
  }

  addPool() {
    if (this.poolsArray.length < 8) {
      const nextId = this.getFirstAvailableId();
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
        stratumV2ChannelType: ['extended'],
        stratumV2AuthorityPubkey: ['', [this.base58Validator()]]
      });

      this.poolsArray.push(poolGroup);
      this.showPassword[nextId] = false;
      this.showAdvancedOptions[nextId] = false;

      // Sort visual cards ascending by ID
      this.poolsArray.controls.sort((a, b) => a.get('id')?.value - b.get('id')?.value);

      const index = this.poolsArray.controls.findIndex(c => c.get('id')?.value === nextId);
      this.setupTlsValidationForIndex(index);
      this.activeCardIndex = index; // Expand newly added pool
      this.form.markAsDirty();
    }
  }

  deletePool(index: number) {
    const poolGroup = this.poolsArray.at(index);
    if (!poolGroup) return;
    const id = poolGroup.get('id')?.value;
    const prim = this.form.get('primaryPoolIndex')?.value;
    const sec = this.form.get('secondaryPoolIndex')?.value;
    if (id === prim || id === sec) {
      this.toastr.error('Cannot delete a pool that is currently selected as primary or fallback.');
      return;
    }

    this.poolsArray.removeAt(index);
    this.pendingDeletePoolIds.push(id);
    this.form.markAsDirty();

    if (this.activeCardIndex === index) {
      this.activeCardIndex = this.poolsArray.length > 0 ? 0 : null;
    } else if (this.activeCardIndex !== null && this.activeCardIndex > index) {
      this.activeCardIndex--;
    }
  }

  isDeleteDisabled(id: number): boolean {
    const prim = this.form?.get('primaryPoolIndex')?.value;
    const sec = this.form?.get('secondaryPoolIndex')?.value;
    return id === prim || id === sec;
  }

  public updateSystem() {
    const form = this.form.getRawValue();

    const restartAlreadyPending = this.savedChanges;

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
          const errorMessage = this.uri ? `Could not save pool settings for ${this.uri}. ${err.message}` : `Could not save pool settings. ${err.message}`;
          this.toastr.error(errorMessage);
          this.savedChanges = restartAlreadyPending;
        }
      });
  }

  private onSaveSuccess() {
    const successMessage = this.uri ? `Saved pool settings for ${this.uri}` : 'Saved pool settings';
    this.toastr.warning('You must restart this device after saving for changes to take effect.');
    this.toastr.success(successMessage);
    this.savedChanges = true;
    this.pendingDeletePoolIds = [];
    this.form.markAsPristine();
  }

  public restart() {
    this.systemService.restart(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Device at ${this.uri} restarted` : 'Device restarted';
          this.toastr.success(successMessage);
          this.savedChanges = false;
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Failed to restart device at ${this.uri}. ${err.message}` : `Failed to restart device. ${err.message}`;
          this.toastr.error(errorMessage);
        }
      });
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

    let isTlsMode = 0;
    const matched = prefixes.find(({ prefix }) => urlValue.startsWith(prefix));
    if (matched) {
      urlValue = urlValue.slice(matched.prefix.length);
      isTlsMode = +matched.tlsMode;
    }

    const { cleanUrl, port } = this.extractPort(urlValue);

    if (port !== undefined) {
      portControl.setValue(port);
    }
    urlControl.setValue(cleanUrl);
    tlsControl.setValue(isTlsMode);
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

  isStandardChannelDisabled(): boolean {
    return this.asicModel === 'BM1397';
  }

  getProtocolLabel(value: string): string {
    const option = this.protocolOptions.find(opt => opt.value === value);
    return option ? option.label : value;
  }
}
