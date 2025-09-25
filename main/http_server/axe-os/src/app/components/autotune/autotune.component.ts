import { Component, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { HttpErrorResponse } from '@angular/common/http';
import { ISystemInfo } from 'src/models/ISystemInfo';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';
import { ToastrService } from 'ngx-toastr';

interface SliderConfig {
  formControlName: string;
  label: string;
  min: number;
  max: number;
  step: number;
  unit: string;
  tooltip: string;
}

@Component({
  selector: 'autotune',
  templateUrl: './autotune.component.html',
})
export class AutotuneComponent implements OnInit {
  public autotuneForm!: FormGroup;

  public sliderConfigs: SliderConfig[] = [
    {
      formControlName: 'power_limit',
      label: 'Power Limit',
      min: 10,
      max: 40,
      step: 1,
      unit: 'W',
      tooltip: 'The maximum power limit that the miner is allowed to use, based on your power supply output. Default:25W'
    },
    {
      formControlName: 'fan_limit',
      label: 'Fan Limit',
      min: 0,
      max: 100,
      step: 1,
      unit: '%',
      tooltip: 'Sets the maximum fan speed limit in percent. This ensures that fans do not exceed this speed, helping to control noise levels and reduce wear on the fans. Default:75%'
    },
    {
      formControlName: 'osh_pow_limit',
      label: 'Overshoot Power Limit',
      min: 0,
      max: 2.2,
      step: 0.1,
      unit: 'W',
      tooltip: 'Maximum allowed power overshoot in watts. This provides a buffer for temporary spikes above the power limit, allowing for brief surges without triggering safety mechanisms. Default:0.2W'
    },
    {
      formControlName: 'osh_fan_limit',
      label: 'Overshoot Fanspeed',
      min: 0,
      max: 100,
      step: 1,
      unit: '%',
      tooltip: 'Maximum allowed fan speed overshoot in percent. This provides a buffer for temporary spikes above the fan limit, allowing for brief increases without triggering safety mechanisms. Default:5%'
    },
    {
      formControlName: 'max_volt_asic',
      label: 'Max Voltage ASIC',
      min: 1000,
      max: 1400,
      step: 1,
      unit: 'mV',
      tooltip: 'Maximum voltage for the ASIC in millivolts. This prevents over-voltage conditions that could damage hardware, ensuring safe operation within specified limits. Default:1400mV'
    },
    {
      formControlName: 'max_freq_asic',
      label: 'Max Frequency ASIC',
      min: 400,
      max: 1000,
      step: 1,
      unit: 'MHz',
      tooltip: 'Maximum frequency for the ASIC in megahertz. This prevents overclocking beyond safe limits, ensuring stable and reliable performance. Default:1000MHz'
    },
    {
      formControlName: 'max_temp_asic',
      label: 'Max Temperature ASIC',
      min: 20,
      max: 80,
      step: 1,
      unit: '°C',
      tooltip: 'Maximum temperature allowed for the ASIC in degrees Celsius. This ensures safe operation and prevents overheating that could damage hardware or affect performance. Default:65°C'
    },
    {
      formControlName: 'max_temp_vr',
      label: 'Max Temperature VR',
      min: 20,
      max: 90,
      step: 1,
      unit: '°C',
      tooltip: 'Maximum temperature for the VoltageRegulator in degrees Celsius. This ensures thermal safety, preventing hardware damage due to overheating. Default:85°C'
    },
    {
      formControlName: 'vf_ratio_max',
      label: 'Max V/F Ratio',
      min: 2.0,
      max: 2.2,
      step: 0.01,
      unit: '',
      tooltip: 'This value sets an upper limit on the Voltage/Frequency ratio that the system is allowed to use. If this ratio is exceeded, the system will adjust the voltage or frequency to stay within this maximum threshold. Default:2.2'
    },
    {
      formControlName: 'vf_ratio_min',
      label: 'Min V/F Ratio',
      min: 1.6,
      max: 2.0,
      step: 0.01,
      unit: '',
      tooltip: ' This value sets a lower limit on the Voltage/Frequency ratio that the system is allowed to use. If this ratio falls below, the system will adjust the voltage or frequency to stay within this minimum threshold. Default:1.76'
    }
  ];

  constructor(
    private fb: FormBuilder,
    private loadingService: LoadingService,
    private systemService: SystemService,
    private toastr: ToastrService
  ) { }

  ngOnInit(): void {
    this.loadAutotuneSettings();
  }

  private loadAutotuneSettings(): void {

    this.systemService.getInfo()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: (info) => {
          // Update the slider config with dynamic minimum value if PID is active
          this.updateSliderMinForPid(info);
        },
        error: () => {
          this.toastr.error('Failed to load getInfog settings');
        }
      });

    this.systemService.getAutotune()
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: autotune => {
          this.autotuneForm = this.fb.group({
            power_limit: [autotune.power_limit, [Validators.required, Validators.min(1)]],
            fan_limit: [autotune.fan_limit, [Validators.required, Validators.min(0)]],
            osh_pow_limit: [autotune.osh_pow_limit],
            osh_fan_limit: [autotune.osh_fan_limit],
            max_volt_asic: [autotune.max_volt_asic, [Validators.required, Validators.min(1)]],
            max_freq_asic: [autotune.max_freq_asic, [Validators.required, Validators.min(1)]],
            max_temp_asic: [autotune.max_temp_asic, [Validators.required, Validators.min(1)]],
            vf_ratio_max: [autotune.vf_ratio_max],
            vf_ratio_min: [autotune.vf_ratio_min],
            max_temp_vr: [autotune.max_temp_vr],
            auto_tune: [autotune.auto_tune ?? false]
          });
        },
        error: () => {
          this.toastr.error('Failed to load autotune settings');
        }
      });
  }

  public updateAutotune(): void {
    this.systemService.updateAutotune(this.autotuneForm.value).subscribe({
      next: () => this.toastr.success('Autotune settings saved!'),
      error: (err: HttpErrorResponse) => this.toastr.error(`Could not save autotune settings. ${err.message}`)
    });
  }

  private updateSliderMinForPid(info: ISystemInfo): void {
    // Check if PID is active (autofanspeed = 1)
    const isPidActive = info.autofanspeed === 1;

    // If PID is active, set the minimum value to (temptarget + 1)
    // Otherwise keep the default minimum of 20
    const minTemp = isPidActive ? (info.temptarget + 1) : 20;

    // Update the slider configuration in our component
    this.sliderConfigs.forEach(config => {
      if (config.formControlName === 'max_temp_asic') {
        config.min = minTemp;
      }
    });
  }
}


