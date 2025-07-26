import { Component, OnInit, ViewChild } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { HttpErrorResponse, HttpEventType } from '@angular/common/http';
import { SystemService } from 'src/app/services/system.service';
import { ToastrService } from 'ngx-toastr';

@Component({
  selector: 'autotune',
  templateUrl: './autotune.component.html',
  styleUrls: ['./autotune.component.scss'] 
})
export class AutotuneComponent implements OnInit {
  constructor(private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,) { }
  public autotuneForm!: FormGroup;
  public autotuneInfo: any = {};


  ngOnInit() {
    this.autotuneForm = this.fb.group({
      auto_tune_hashrate: [true, [Validators.required, Validators.required]],
      power_limit: [20, [Validators.required, Validators.min(1)]],
      fan_limit: [75, [Validators.required, Validators.min(1)]],
      max_voltage_asic: [1400, [Validators.required, Validators.min(1)]],
      max_frequency_asic: [1000, [Validators.required, Validators.min(1)]],
      max_asic_temperatur: [65, [Validators.required, Validators.min(1)]],
      overshot_power_limit: [0.2],  
      overshot_fanspeed: [5],
      vf_ratio_max: [2.2],
      vf_ratio_min: [1.76],
    });

    // Load autotune settings from API and patch the form if available
    this.systemService.getAutotune().subscribe({
      next: autotune => {
        this.autotuneInfo = autotune;
        this.autotuneForm.patchValue({
          auto_tune_hashrate: autotune.auto_tune_hashrate,
          power_limit: autotune.power_limit ?? 20,
          fan_limit: autotune.fan_limit ?? 75,
          max_voltage_asic: autotune.max_voltage_asic ?? 1400,
          max_frequency_asic: autotune.max_frequency_asic ?? 1000,
          max_asic_temperatur: autotune.max_asic_temperatur ?? 65,
          overshot_power_limit: autotune.overshot_power_limit ?? 0.2,
          overshot_fanspeed: autotune.overshot_fanspeed ?? 5,
          vf_ratio_max: autotune.vf_ratio_max ?? 2.2,
          vf_ratio_min: autotune.vf_ratio_min ?? 1.76,
        });
      },
      error: err => { this.toastr.error('Failed to load autotune settings'); }
    });
  }



  public updateAutotune() {
    if (!this.autotuneForm.valid) return;
    this.systemService.updateAutotune(this.autotuneForm.value)
      .subscribe({
        next: () => this.toastr.success('Autotune settings saved!'),
        error: (err: HttpErrorResponse) => {this.toastr.error(`Could not save autotune settings. ${err.message}`);}
      });
  }
}